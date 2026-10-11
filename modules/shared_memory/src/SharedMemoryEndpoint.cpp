#include "SharedMemoryEndpoint.h"

#include <cstring>

#include <QThread>
#include <QtEndian>
#include <QByteArray>
#include <QElapsedTimer>
#include <QAtomicInteger>
#include <QMetaObject>
#include <QSharedMemory>
#include <QSystemSemaphore>
#include <QTimer>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(lcSharedMemoryEndpoint, "booking.modules.sharedmemory.endpoint")

namespace {
constexpr quint32 kMagic = 0x42525742U; // BRWB
constexpr quint32 kQueueLayoutVersion = 2;
constexpr qsizetype kQueueHeaderSize = 32;
constexpr qsizetype kFrameHeaderSize = 4;
constexpr qsizetype kSegmentSize =
        SharedMemoryProtocol::kMaxPayloadSize + kQueueHeaderSize + kFrameHeaderSize;
constexpr int kSafetyPollIntervalMs = 250;
constexpr int kFallbackPollIntervalMs = 50;
constexpr int kMaxFramesPerPoll = 64;
constexpr qint64 kPollTimeBudgetMs = 5;

constexpr qsizetype kMagicOffset = 0;
constexpr qsizetype kProtocolVersionOffset = 4;
constexpr qsizetype kLayoutVersionOffset = 8;
constexpr qsizetype kCapacityOffset = 12;
constexpr qsizetype kHeadOffset = 16;
constexpr qsizetype kTailOffset = 20;
constexpr qsizetype kUsedOffset = 24;

void writeU32(char *target, qsizetype offset, quint32 value)
{
    qToLittleEndian(value, reinterpret_cast<uchar *>(target + offset));
}

quint32 readU32(const char *source, qsizetype offset)
{
    return qFromLittleEndian<quint32>(reinterpret_cast<const uchar *>(source + offset));
}

qsizetype queueCapacity()
{
    return kSegmentSize - kQueueHeaderSize;
}

char *queueData(char *segmentData)
{
    return segmentData + kQueueHeaderSize;
}

const char *queueData(const char *segmentData)
{
    return segmentData + kQueueHeaderSize;
}

} // namespace

class SharedMemoryEndpoint::Channel
{
public:
    Channel(const QString &memoryKey, const QString &semaphoreKey, const QString &notificationKey);
    ~Channel();

    QSharedMemory memory;
    QSystemSemaphore semaphore;
    QSystemSemaphore notifier;
};

SharedMemoryEndpoint::Channel::Channel(const QString &memoryKey,
                                       const QString &semaphoreKey,
                                       const QString &notificationKey)
    : memory(memoryKey)
    , semaphore(semaphoreKey, 1, QSystemSemaphore::Open)
    , notifier(notificationKey, 0, QSystemSemaphore::Open)
{
    // do nothing
}

SharedMemoryEndpoint::Channel::~Channel()
{
    if (memory.isAttached())
        memory.detach();
}

class SharedMemoryEndpoint::NotificationWorker final : public QThread
{
public:
    NotificationWorker(const QString &notificationKey, SharedMemoryEndpoint *endpoint);
    ~NotificationWorker() override;

    void stop();

protected:
    void run() override;

private:
    QSystemSemaphore m_notifier;
    SharedMemoryEndpoint *m_endpoint;
    QAtomicInteger<int> m_stopRequested;
};

SharedMemoryEndpoint::NotificationWorker::NotificationWorker(const QString &notificationKey,
                                                             SharedMemoryEndpoint *endpoint)
    : QThread(endpoint)
    , m_notifier(notificationKey, 0, QSystemSemaphore::Open)
    , m_endpoint(endpoint)
    , m_stopRequested(0)
{
    // do nothing
}

SharedMemoryEndpoint::NotificationWorker::~NotificationWorker()
{
    stop();
}

void SharedMemoryEndpoint::NotificationWorker::stop()
{
    m_stopRequested.storeRelease(1);
    if (isRunning() && !m_notifier.release()) {
        qCWarning(lcSharedMemoryEndpoint).noquote()
                << QStringLiteral("Failed to stop shared memory notification worker '%1': %2")
                           .arg(m_notifier.key(), m_notifier.errorString());
    }
    wait();
}

void SharedMemoryEndpoint::NotificationWorker::run()
{
    while (true) {
        if (!m_notifier.acquire()) {
            if (!m_stopRequested.loadAcquire()) {
                const QString key = m_notifier.key();
                const QString description = m_notifier.errorString();
                QMetaObject::invokeMethod(
                        m_endpoint,
                        [endpoint = m_endpoint, key, description]() {
                            endpoint->handleNotificationError(key, description);
                        },
                        Qt::QueuedConnection);
            }
            return;
        }
        if (m_stopRequested.loadAcquire())
            return;
        m_endpoint->schedulePoll();
    }
}

SharedMemoryEndpoint::SharedMemoryEndpoint(Role role, const QString &baseKey, QObject *parent)
    : QObject(parent)
    , m_role(role)
    , m_baseKey(normalizeBaseKey(baseKey))
    , m_authenticationSecret()
    , m_incoming(nullptr)
    , m_outgoing(nullptr)
    , m_notificationWorker(nullptr)
    , m_fallbackPollTimer(new QTimer(this))
    , m_receivedFrameCount(0)
    , m_sentFrameCount(0)
    , m_busyWriteCount(0)
    , m_receivedByteCount(0)
    , m_sentByteCount(0)
    , m_pollScheduled(0)
    , m_notificationErrorCount(0)
    , m_coalescedWakeupCount(0)
    , m_pollBatchCount(0)
    , m_running(false)
{
    m_fallbackPollTimer->setInterval(kSafetyPollIntervalMs);
    QObject::connect(m_fallbackPollTimer, &QTimer::timeout, this, [this]() { schedulePoll(); });
}

SharedMemoryEndpoint::~SharedMemoryEndpoint()
{
    stop();
}

SharedMemoryEndpoint::Role SharedMemoryEndpoint::role() const
{
    return m_role;
}

QString SharedMemoryEndpoint::baseKey() const
{
    return m_baseKey;
}

void SharedMemoryEndpoint::setBaseKey(const QString &baseKey)
{
    if (m_running)
        return;
    m_baseKey = normalizeBaseKey(baseKey);
}

void SharedMemoryEndpoint::setAuthenticationSecret(const QByteArray &secret)
{
    if (m_running)
        return;
    m_authenticationSecret = secret;
}

bool SharedMemoryEndpoint::isRunning() const
{
    return m_running;
}

QString SharedMemoryEndpoint::lastError() const
{
    return m_lastError;
}

quint64 SharedMemoryEndpoint::receivedFrameCount() const
{
    return m_receivedFrameCount;
}

quint64 SharedMemoryEndpoint::sentFrameCount() const
{
    return m_sentFrameCount;
}

quint64 SharedMemoryEndpoint::busyWriteCount() const
{
    return m_busyWriteCount;
}

quint64 SharedMemoryEndpoint::receivedByteCount() const
{
    return m_receivedByteCount;
}

quint64 SharedMemoryEndpoint::sentByteCount() const
{
    return m_sentByteCount;
}

quint64 SharedMemoryEndpoint::notificationErrorCount() const
{
    return m_notificationErrorCount.loadAcquire();
}

quint64 SharedMemoryEndpoint::coalescedWakeupCount() const
{
    return m_coalescedWakeupCount.loadAcquire();
}

quint64 SharedMemoryEndpoint::pollBatchCount() const
{
    return m_pollBatchCount.loadAcquire();
}

bool SharedMemoryEndpoint::start()
{
    if (thread() != QThread::currentThread()) {
        bool result = false;
        QMetaObject::invokeMethod(
                this, [this, &result]() { result = start(); }, Qt::BlockingQueuedConnection);
        return result;
    }

    if (m_running)
        return true;

    m_fallbackPollTimer->stop();
    m_fallbackPollTimer->setInterval(kSafetyPollIntervalMs);
    m_pollScheduled.storeRelease(0);
    m_notificationErrorCount.storeRelease(0);
    m_coalescedWakeupCount.storeRelease(0);
    m_pollBatchCount.storeRelease(0);

    if (m_baseKey.isEmpty()) {
        setLastError(tr("Shared memory key is empty."));
        return false;
    }

    const QString requestKey = channelKey(QStringLiteral("requests"));
    const QString responseKey = channelKey(QStringLiteral("responses"));

    delete m_incoming;
    delete m_outgoing;
    m_incoming = nullptr;
    m_outgoing = nullptr;

    if (m_role == Role::Server) {
        m_incoming = new Channel(requestKey,
                                 requestKey + QStringLiteral(".lock"),
                                 requestKey + QStringLiteral(".ready"));
        m_outgoing = new Channel(responseKey,
                                 responseKey + QStringLiteral(".lock"),
                                 responseKey + QStringLiteral(".ready"));
    } else {
        m_incoming = new Channel(responseKey,
                                 responseKey + QStringLiteral(".lock"),
                                 responseKey + QStringLiteral(".ready"));
        m_outgoing = new Channel(requestKey,
                                 requestKey + QStringLiteral(".lock"),
                                 requestKey + QStringLiteral(".ready"));
    }

    const bool createSegments = (m_role == Role::Server);
    if (!openChannel(m_incoming, createSegments) || !openChannel(m_outgoing, createSegments)) {
        stop();
        return false;
    }

    m_running = true;
    m_notificationWorker =
            new NotificationWorker(m_role == Role::Server ? requestKey + QStringLiteral(".ready")
                                                          : responseKey + QStringLiteral(".ready"),
                                   this);
    m_notificationWorker->start();
    poll();
    m_fallbackPollTimer->start();
    return true;
}

void SharedMemoryEndpoint::stop()
{
    if (thread() != QThread::currentThread()) {
        QMetaObject::invokeMethod(this, [this]() { stop(); }, Qt::BlockingQueuedConnection);
        return;
    }

    m_running = false;
    m_fallbackPollTimer->stop();
    m_pollScheduled.storeRelease(0);

    if (m_notificationWorker) {
        m_notificationWorker->stop();
        delete m_notificationWorker;
        m_notificationWorker = nullptr;
    }

    delete m_incoming;
    delete m_outgoing;
    m_incoming = nullptr;
    m_outgoing = nullptr;
}

bool SharedMemoryEndpoint::sendFrame(const SharedMemoryProtocol::Frame &frame)
{
    return sendFrameStatus(frame) == SendStatus::Success;
}

SharedMemoryEndpoint::SendStatus SharedMemoryEndpoint::sendFrameStatus(
        const SharedMemoryProtocol::Frame &frame)
{
    if (!ensureStartedForSend())
        return SendStatus::Failed;
    return writeFrame(m_outgoing, frame);
}

void SharedMemoryEndpoint::poll()
{
    m_pollScheduled.storeRelease(0);
    if (!m_running || !m_incoming)
        return;

    m_pollBatchCount.fetchAndAddRelaxed(1);
    QElapsedTimer budget;
    budget.start();
    int processedItems = 0;
    bool queueDrained = false;
    while (processedItems < kMaxFramesPerPoll && budget.elapsed() < kPollTimeBudgetMs) {
        SharedMemoryProtocol::Frame frame;
        const ReadStatus status = readFrame(m_incoming, &frame);
        if (status == ReadStatus::Empty) {
            queueDrained = true;
            break;
        }
        if (status == ReadStatus::Failed) {
            m_fallbackPollTimer->setInterval(kFallbackPollIntervalMs);
            break;
        }

        ++processedItems;
        if (status == ReadStatus::FrameRead)
            emit frameReceived(frame);
    }

    if (!queueDrained && processedItems > 0
        && (processedItems >= kMaxFramesPerPoll || budget.elapsed() >= kPollTimeBudgetMs)) {
        schedulePoll();
    }
}

void SharedMemoryEndpoint::schedulePoll()
{
    if (!m_pollScheduled.testAndSetOrdered(0, 1)) {
        m_coalescedWakeupCount.fetchAndAddRelaxed(1);
        return;
    }

    if (!QMetaObject::invokeMethod(this, "poll", Qt::QueuedConnection))
        m_pollScheduled.storeRelease(0);
}

void SharedMemoryEndpoint::handleNotificationError(const QString &key, const QString &description)
{
    if (!m_running)
        return;

    m_notificationErrorCount.fetchAndAddRelaxed(1);
    const QString detail = description.trimmed().isEmpty()
                                   ? QStringLiteral("unknown system semaphore error")
                                   : description.trimmed();
    setLastError(QStringLiteral("Shared memory notification semaphore '%1' failed: %2. "
                                "Falling back to periodic polling.")
                         .arg(key, detail));
    m_fallbackPollTimer->setInterval(kFallbackPollIntervalMs);
    if (!m_fallbackPollTimer->isActive())
        m_fallbackPollTimer->start();
    schedulePoll();
}

QString SharedMemoryEndpoint::normalizeBaseKey(const QString &key) const
{
    QString normalized = key.trimmed();
    if (normalized.isEmpty())
        normalized = QStringLiteral("booking-resources-web");
    return normalized;
}

QString SharedMemoryEndpoint::channelKey(const QString &name) const
{
    return QStringLiteral("%1.%2").arg(m_baseKey, name);
}

bool SharedMemoryEndpoint::openChannel(Channel *channel, bool createSegment)
{
    if (!channel)
        return false;

    if (createSegment) {
        if (channel->memory.create(kSegmentSize)) {
            return ensureQueue(channel, true);
        }

        if (channel->memory.error() != QSharedMemory::AlreadyExists) {
            setLastError(tr("Failed to create shared memory segment '%1': %2")
                                 .arg(channel->memory.key(), channel->memory.errorString()));
            return false;
        }
    }

    if (!channel->memory.isAttached() && !channel->memory.attach()) {
        setLastError(tr("Failed to attach shared memory segment '%1': %2")
                             .arg(channel->memory.key(), channel->memory.errorString()));
        return false;
    }
    return ensureQueue(channel, createSegment);
}

bool SharedMemoryEndpoint::ensureQueue(Channel *channel, bool reset)
{
    if (!channel || !channel->memory.isAttached())
        return false;
    if (channel->memory.size() < kSegmentSize) {
        setLastError(tr("Shared memory segment '%1' is smaller than the current IPC layout.")
                             .arg(channel->memory.key()));
        return false;
    }

    if (!lockChannel(channel))
        return false;

    char *data = static_cast<char *>(channel->memory.data());
    const char *constData = static_cast<const char *>(channel->memory.constData());
    const quint32 capacity = static_cast<quint32>(queueCapacity());
    const bool valid = readU32(constData, kMagicOffset) == kMagic
                       && readU32(constData, kProtocolVersionOffset)
                                  == static_cast<quint32>(SharedMemoryProtocol::kProtocolVersion)
                       && readU32(constData, kLayoutVersionOffset) == kQueueLayoutVersion
                       && readU32(constData, kCapacityOffset) == capacity
                       && readU32(constData, kHeadOffset) < capacity
                       && readU32(constData, kTailOffset) < capacity
                       && readU32(constData, kUsedOffset) <= capacity;

    if (reset || !valid) {
        std::memset(data, 0, static_cast<size_t>(kSegmentSize));
        writeU32(data, kMagicOffset, kMagic);
        writeU32(data, kProtocolVersionOffset, SharedMemoryProtocol::kProtocolVersion);
        writeU32(data, kLayoutVersionOffset, kQueueLayoutVersion);
        writeU32(data, kCapacityOffset, capacity);
        writeU32(data, kHeadOffset, 0);
        writeU32(data, kTailOffset, 0);
        writeU32(data, kUsedOffset, 0);
    }

    return unlockChannel(channel);
}

bool SharedMemoryEndpoint::ensureStartedForSend()
{
    if (m_running)
        return true;
    return start();
}

bool SharedMemoryEndpoint::lockChannel(Channel *channel)
{
    if (!channel)
        return false;

    if (!channel->semaphore.acquire()) {
        setLastError(QStringLiteral("Failed to acquire shared memory channel semaphore '%1': %2")
                             .arg(channel->semaphore.key(), channel->semaphore.errorString()));
        return false;
    }

    if (channel->memory.lock())
        return true;

    QString description = QStringLiteral("Failed to lock shared memory segment '%1': %2")
                                  .arg(channel->memory.key(), channel->memory.errorString());
    if (!channel->semaphore.release()) {
        description += QStringLiteral("; failed to release channel semaphore '%1': %2")
                               .arg(channel->semaphore.key(), channel->semaphore.errorString());
    }
    setLastError(description);
    return false;
}

bool SharedMemoryEndpoint::unlockChannel(Channel *channel)
{
    if (!channel)
        return false;

    const bool memoryUnlocked = channel->memory.unlock();
    const QString memoryError = memoryUnlocked ? QString() : channel->memory.errorString();
    const bool semaphoreReleased = channel->semaphore.release();
    const QString semaphoreError = semaphoreReleased ? QString() : channel->semaphore.errorString();
    if (memoryUnlocked && semaphoreReleased)
        return true;

    QString description;
    if (!memoryUnlocked) {
        description = QStringLiteral("Failed to unlock shared memory segment '%1': %2")
                              .arg(channel->memory.key(), memoryError);
    }
    if (!semaphoreReleased) {
        if (!description.isEmpty())
            description += QStringLiteral("; ");
        description += QStringLiteral("failed to release channel semaphore '%1': %2")
                               .arg(channel->semaphore.key(), semaphoreError);
    }
    setLastError(description);
    return false;
}

SharedMemoryEndpoint::ReadStatus SharedMemoryEndpoint::readFrame(Channel *channel,
                                                                 SharedMemoryProtocol::Frame *frame)
{
    if (!channel || !frame || !channel->memory.isAttached())
        return ReadStatus::Failed;

    if (!lockChannel(channel))
        return ReadStatus::Failed;

    char *segmentData = static_cast<char *>(channel->memory.data());
    const char *constSegmentData = static_cast<const char *>(channel->memory.constData());
    const quint32 capacity = static_cast<quint32>(queueCapacity());
    const bool headerValid =
            readU32(constSegmentData, kMagicOffset) == kMagic
            && readU32(constSegmentData, kProtocolVersionOffset)
                       == static_cast<quint32>(SharedMemoryProtocol::kProtocolVersion)
            && readU32(constSegmentData, kLayoutVersionOffset) == kQueueLayoutVersion
            && readU32(constSegmentData, kCapacityOffset) == capacity;

    if (!headerValid) {
        std::memset(segmentData, 0, static_cast<size_t>(kSegmentSize));
        writeU32(segmentData, kMagicOffset, kMagic);
        writeU32(segmentData, kProtocolVersionOffset, SharedMemoryProtocol::kProtocolVersion);
        writeU32(segmentData, kLayoutVersionOffset, kQueueLayoutVersion);
        writeU32(segmentData, kCapacityOffset, capacity);
        const bool unlocked = unlockChannel(channel);
        setLastError(
                tr("Shared memory channel '%1' header is invalid.").arg(channel->memory.key()));
        return unlocked ? ReadStatus::FrameDiscarded : ReadStatus::Failed;
    }

    quint32 head = readU32(constSegmentData, kHeadOffset);
    quint32 used = readU32(constSegmentData, kUsedOffset);
    if (used == 0)
        return unlockChannel(channel) ? ReadStatus::Empty : ReadStatus::Failed;

    if (head >= capacity || used > capacity) {
        std::memset(segmentData, 0, static_cast<size_t>(kSegmentSize));
        writeU32(segmentData, kMagicOffset, kMagic);
        writeU32(segmentData, kProtocolVersionOffset, SharedMemoryProtocol::kProtocolVersion);
        writeU32(segmentData, kLayoutVersionOffset, kQueueLayoutVersion);
        writeU32(segmentData, kCapacityOffset, capacity);
        const bool unlocked = unlockChannel(channel);
        setLastError(
                tr("Shared memory channel '%1' header is invalid.").arg(channel->memory.key()));
        return unlocked ? ReadStatus::FrameDiscarded : ReadStatus::Failed;
    }

    const char *constData = queueData(constSegmentData);
    while (used > 0 && capacity - head < static_cast<quint32>(kFrameHeaderSize)) {
        const quint32 padding = capacity - head;
        if (used < padding) {
            used = 0;
            break;
        }
        used -= padding;
        head = 0;
    }
    if (used == 0) {
        writeU32(segmentData, kHeadOffset, head);
        writeU32(segmentData, kUsedOffset, used);
        return unlockChannel(channel) ? ReadStatus::Empty : ReadStatus::Failed;
    }

    quint32 payloadSize = readU32(constData, head);
    if (payloadSize == 0) {
        const quint32 padding = capacity - head;
        if (used < padding) {
            used = 0;
        } else {
            used -= padding;
        }
        head = 0;
        if (used == 0) {
            writeU32(segmentData, kHeadOffset, head);
            writeU32(segmentData, kUsedOffset, used);
            return unlockChannel(channel) ? ReadStatus::Empty : ReadStatus::Failed;
        }
        payloadSize = readU32(constData, head);
    }

    const quint32 frameSize = payloadSize + static_cast<quint32>(kFrameHeaderSize);
    if (payloadSize == 0
        || payloadSize > static_cast<quint32>(SharedMemoryProtocol::kMaxPayloadSize)
        || frameSize > used || head + frameSize > capacity) {
        std::memset(segmentData, 0, static_cast<size_t>(kSegmentSize));
        writeU32(segmentData, kMagicOffset, kMagic);
        writeU32(segmentData, kProtocolVersionOffset, SharedMemoryProtocol::kProtocolVersion);
        writeU32(segmentData, kLayoutVersionOffset, kQueueLayoutVersion);
        writeU32(segmentData, kCapacityOffset, capacity);
        const bool unlocked = unlockChannel(channel);
        setLastError(tr("Shared memory channel '%1' frame is invalid.").arg(channel->memory.key()));
        return unlocked ? ReadStatus::FrameDiscarded : ReadStatus::Failed;
    }

    const QByteArray payload(constData + head + kFrameHeaderSize, static_cast<int>(payloadSize));
    head += frameSize;
    if (head == capacity)
        head = 0;
    used -= frameSize;
    writeU32(segmentData, kHeadOffset, head);
    writeU32(segmentData, kUsedOffset, used);
    if (!unlockChannel(channel))
        return ReadStatus::Failed;

    QString parseError;
    if (!SharedMemoryProtocol::parse(payload, frame, &parseError, m_authenticationSecret)) {
        setLastError(tr("Failed to parse shared memory frame: %1").arg(parseError));
        return ReadStatus::FrameDiscarded;
    }
    ++m_receivedFrameCount;
    m_receivedByteCount += static_cast<quint64>(payload.size());
    return ReadStatus::FrameRead;
}

SharedMemoryEndpoint::SendStatus SharedMemoryEndpoint::writeFrame(
        Channel *channel, const SharedMemoryProtocol::Frame &frame)
{
    if (!channel || !channel->memory.isAttached())
        return SendStatus::Failed;

    const QByteArray payload = SharedMemoryProtocol::serialize(frame, m_authenticationSecret);
    if (payload.isEmpty()) {
        setLastError(tr("Cannot serialize shared memory frame."));
        return SendStatus::Failed;
    }

    if (!lockChannel(channel))
        return SendStatus::Failed;

    char *segmentData = static_cast<char *>(channel->memory.data());
    const char *constSegmentData = static_cast<const char *>(channel->memory.constData());
    const quint32 capacity = static_cast<quint32>(queueCapacity());
    const bool headerValid =
            readU32(constSegmentData, kMagicOffset) == kMagic
            && readU32(constSegmentData, kProtocolVersionOffset)
                       == static_cast<quint32>(SharedMemoryProtocol::kProtocolVersion)
            && readU32(constSegmentData, kLayoutVersionOffset) == kQueueLayoutVersion
            && readU32(constSegmentData, kCapacityOffset) == capacity;

    if (!headerValid) {
        std::memset(segmentData, 0, static_cast<size_t>(kSegmentSize));
        writeU32(segmentData, kMagicOffset, kMagic);
        writeU32(segmentData, kProtocolVersionOffset, SharedMemoryProtocol::kProtocolVersion);
        writeU32(segmentData, kLayoutVersionOffset, kQueueLayoutVersion);
        writeU32(segmentData, kCapacityOffset, capacity);
    }

    quint32 head = readU32(constSegmentData, kHeadOffset);
    quint32 tail = readU32(constSegmentData, kTailOffset);
    quint32 used = readU32(constSegmentData, kUsedOffset);
    if (head >= capacity || tail >= capacity || used > capacity) {
        std::memset(segmentData, 0, static_cast<size_t>(kSegmentSize));
        writeU32(segmentData, kMagicOffset, kMagic);
        writeU32(segmentData, kProtocolVersionOffset, SharedMemoryProtocol::kProtocolVersion);
        writeU32(segmentData, kLayoutVersionOffset, kQueueLayoutVersion);
        writeU32(segmentData, kCapacityOffset, capacity);
        head = 0;
        tail = 0;
        used = 0;
    }

    const quint32 frameSize = static_cast<quint32>(payload.size() + kFrameHeaderSize);
    if (frameSize > capacity) {
        unlockChannel(channel);
        setLastError(tr("Shared memory frame exceeds channel capacity."));
        return SendStatus::Failed;
    }

    quint32 padding = 0;
    if (capacity - tail < frameSize)
        padding = capacity - tail;

    if (capacity - used < frameSize + padding) {
        if (!unlockChannel(channel))
            return SendStatus::Failed;
        // The peer has not consumed enough frames yet. This is normal backpressure; callers with
        // an outgoing queue should retry later.
        ++m_busyWriteCount;
        m_lastError = tr("Shared memory channel '%1' is busy.").arg(channel->memory.key());
        return SendStatus::Busy;
    }

    char *data = queueData(segmentData);
    if (padding > 0) {
        if (padding >= static_cast<quint32>(kFrameHeaderSize))
            writeU32(data, tail, 0);
        tail = 0;
        used += padding;
    }

    writeU32(data, tail, static_cast<quint32>(payload.size()));
    std::memcpy(data + tail + kFrameHeaderSize,
                payload.constData(),
                static_cast<size_t>(payload.size()));
    tail += frameSize;
    if (tail == capacity)
        tail = 0;
    used += frameSize;

    writeU32(segmentData, kHeadOffset, head);
    writeU32(segmentData, kTailOffset, tail);
    writeU32(segmentData, kUsedOffset, used);

    if (!unlockChannel(channel))
        return SendStatus::Failed;
    if (!channel->notifier.release()) {
        setLastError(
                QStringLiteral("Failed to release shared memory notification semaphore '%1': %2")
                        .arg(channel->notifier.key(), channel->notifier.errorString()));
    }
    ++m_sentFrameCount;
    m_sentByteCount += static_cast<quint64>(payload.size());
    return SendStatus::Success;
}

void SharedMemoryEndpoint::setLastError(const QString &description)
{
    m_lastError = description;
    if (!description.isEmpty()) {
        qCWarning(lcSharedMemoryEndpoint).noquote() << description;
        emit errorOccurred(description);
    }
}
