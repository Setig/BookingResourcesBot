#include "CliLineEditor.h"

#include "CliHistory.h"

#include <QByteArray>

#ifdef Q_OS_WIN
#  include <windows.h>
#elif defined(Q_OS_UNIX)
#  include <errno.h>
#  include <termios.h>
#  include <unistd.h>
#endif

namespace {

#ifdef Q_OS_WIN
class WindowsRawModeGuard
{
public:
    WindowsRawModeGuard()
        : m_handle(GetStdHandle(STD_INPUT_HANDLE))
        , m_originalMode(0)
        , m_active(false)
    {
        if (m_handle == INVALID_HANDLE_VALUE || !GetConsoleMode(m_handle, &m_originalMode))
            return;

        DWORD rawMode = m_originalMode;
        rawMode &= ~(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT);
        rawMode |= ENABLE_PROCESSED_INPUT;
        if (!SetConsoleMode(m_handle, rawMode))
            return;
        m_active = true;
    }

    ~WindowsRawModeGuard()
    {
        if (m_active && m_handle != INVALID_HANDLE_VALUE)
            SetConsoleMode(m_handle, m_originalMode);
    }

    WindowsRawModeGuard(const WindowsRawModeGuard &) = delete;
    WindowsRawModeGuard &operator=(const WindowsRawModeGuard &) = delete;

    [[nodiscard]] bool isActive() const { return m_active; }

    [[nodiscard]] HANDLE handle() const { return m_handle; }

private:
    HANDLE m_handle;
    DWORD m_originalMode;
    bool m_active;
};
#elif defined(Q_OS_UNIX)
class UnixRawModeGuard
{
public:
    UnixRawModeGuard()
        : m_active(false)
    {
        if (!isatty(STDIN_FILENO))
            return;
        if (tcgetattr(STDIN_FILENO, &m_original) != 0)
            return;

        termios raw = m_original;
        raw.c_lflag &= static_cast<tcflag_t>(~(ICANON | ECHO));
        raw.c_cc[VMIN] = 1;
        raw.c_cc[VTIME] = 0;
        if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) != 0)
            return;
        m_active = true;
    }

    ~UnixRawModeGuard()
    {
        if (m_active)
            tcsetattr(STDIN_FILENO, TCSANOW, &m_original);
    }

    UnixRawModeGuard(const UnixRawModeGuard &) = delete;
    UnixRawModeGuard &operator=(const UnixRawModeGuard &) = delete;

    [[nodiscard]] bool isActive() const { return m_active; }

private:
    termios m_original;
    bool m_active;
};
#endif

} // namespace

CliLineEditor::CliLineEditor()
    : m_output(nullptr)
    , m_fallbackInput(nullptr)
    , m_history(nullptr)
    , m_promptVisibleWidth(0)
    , m_cursor(0)
{
    // do nothing
}

CliLineEditor::~CliLineEditor()
{
    // do nothing
}

void CliLineEditor::setOutput(QTextStream *output)
{
    m_output = output;
}

void CliLineEditor::setFallbackInput(QTextStream *input)
{
    m_fallbackInput = input;
}

void CliLineEditor::setPrompt(const QString &prompt, int visibleWidth)
{
    m_prompt = prompt;
    m_promptVisibleWidth = qMax(0, visibleWidth);
}

void CliLineEditor::setHistory(CliHistory *history)
{
    m_history = history;
}

void CliLineEditor::setCompletionHandler(CompletionHandler handler)
{
    m_completionHandler = std::move(handler);
}

void CliLineEditor::setInterruptCheck(InterruptCheck check)
{
    m_interruptCheck = std::move(check);
}

CliLineEditor::Status CliLineEditor::readLine(QString *line)
{
    if (line)
        line->clear();

    m_buffer.clear();
    m_cursor = 0;
    if (m_history)
        m_history->resetNavigation();

    if (!isInteractive())
        return readFallbackLine(line);

    return readRawLine(line);
}

bool CliLineEditor::isInteractive() const
{
#ifdef Q_OS_WIN
    HANDLE handle = GetStdHandle(STD_INPUT_HANDLE);
    DWORD mode = 0;
    return handle != INVALID_HANDLE_VALUE && GetConsoleMode(handle, &mode);
#elif defined(Q_OS_UNIX)
    return isatty(STDIN_FILENO) != 0;
#else
    return false;
#endif
}

bool CliLineEditor::interruptRequested()
{
    return m_interruptCheck && m_interruptCheck();
}

CliLineEditor::Status CliLineEditor::readFallbackLine(QString *line)
{
    if (interruptRequested())
        return Status::Interrupted;
    if (!m_fallbackInput)
        return Status::Eof;

    write(m_prompt);
    flush();
    const QString value = m_fallbackInput->readLine();
    if (interruptRequested())
        return Status::Interrupted;
    if (value.isNull())
        return Status::Eof;
    if (line)
        *line = value;
    return Status::Line;
}

CliLineEditor::Status CliLineEditor::readRawLine(QString *line)
{
#ifdef Q_OS_WIN
    WindowsRawModeGuard guard;
    if (!guard.isActive())
        return readFallbackLine(line);
#elif defined(Q_OS_UNIX)
    UnixRawModeGuard guard;
    if (!guard.isActive())
        return readFallbackLine(line);
#else
    return readFallbackLine(line);
#endif

    redraw();
    while (true) {
        if (interruptRequested())
            return Status::Interrupted;

        KeyEvent event;
        event.key = Key::None;
        if (!readKey(&event)) {
            if (interruptRequested())
                return Status::Interrupted;
            return Status::Eof;
        }
        if (interruptRequested())
            return Status::Interrupted;

        switch (event.key) {
        case Key::Enter:
            write(QStringLiteral("\n"));
            flush();
            if (m_history)
                m_history->add(m_buffer);
            if (line)
                *line = m_buffer;
            return Status::Line;
        case Key::Interrupt:
            return Status::Interrupted;
        case Key::Eof:
            if (m_buffer.isEmpty()) {
                write(QStringLiteral("\n"));
                flush();
                return Status::Eof;
            }
            break;
        case Key::Backspace:
            deleteBeforeCursor();
            break;
        case Key::Delete:
            deleteAtCursor();
            break;
        case Key::Left:
            moveLeft();
            break;
        case Key::Right:
            moveRight();
            break;
        case Key::Home:
            moveHome();
            break;
        case Key::End:
            moveEnd();
            break;
        case Key::Up:
            historyUp();
            break;
        case Key::Down:
            historyDown();
            break;
        case Key::Tab:
            complete();
            break;
        case Key::Character:
            if (!event.character.isNull() && event.character.unicode() >= 32)
                insertText(QString(event.character));
            break;
        case Key::None:
            break;
        }
    }
}

bool CliLineEditor::readKey(KeyEvent *event)
{
    if (!event)
        return false;
    event->key = Key::None;
    event->character = QChar();

#ifdef Q_OS_WIN
    HANDLE handle = GetStdHandle(STD_INPUT_HANDLE);
    if (handle == INVALID_HANDLE_VALUE)
        return false;

    INPUT_RECORD record;
    DWORD readCount = 0;
    if (!ReadConsoleInputW(handle, &record, 1, &readCount)) {
        const DWORD error = GetLastError();
        if (error == ERROR_OPERATION_ABORTED)
            event->key = Key::Interrupt;
        return error == ERROR_OPERATION_ABORTED;
    }
    if (readCount == 0)
        return false;
    if (record.EventType != KEY_EVENT || !record.Event.KeyEvent.bKeyDown)
        return true;

    const WORD virtualKey = record.Event.KeyEvent.wVirtualKeyCode;
    const WCHAR unicode = record.Event.KeyEvent.uChar.UnicodeChar;
    switch (virtualKey) {
    case VK_RETURN:
        event->key = Key::Enter;
        return true;
    case VK_BACK:
        event->key = Key::Backspace;
        return true;
    case VK_DELETE:
        event->key = Key::Delete;
        return true;
    case VK_LEFT:
        event->key = Key::Left;
        return true;
    case VK_RIGHT:
        event->key = Key::Right;
        return true;
    case VK_HOME:
        event->key = Key::Home;
        return true;
    case VK_END:
        event->key = Key::End;
        return true;
    case VK_UP:
        event->key = Key::Up;
        return true;
    case VK_DOWN:
        event->key = Key::Down;
        return true;
    case VK_TAB:
        event->key = Key::Tab;
        return true;
    default:
        break;
    }

    if (unicode == 3) {
        event->key = Key::Interrupt;
        return true;
    }
    if (unicode == 4) {
        event->key = Key::Eof;
        return true;
    }
    if (unicode == 1) {
        event->key = Key::Home;
        return true;
    }
    if (unicode == 5) {
        event->key = Key::End;
        return true;
    }
    if (unicode >= 32) {
        event->key = Key::Character;
        event->character = QChar(static_cast<ushort>(unicode));
    }
    return true;
#elif defined(Q_OS_UNIX)
    unsigned char first = 0;
    const ssize_t n = ::read(STDIN_FILENO, &first, 1);
    if (n < 0) {
        if (errno == EINTR) {
            event->key = Key::Interrupt;
            return true;
        }
        return false;
    }
    if (n == 0)
        return false;

    if (first == 3) {
        event->key = Key::Interrupt;
        return true;
    }
    if (first == 4) {
        event->key = Key::Eof;
        return true;
    }
    if (first == 1) {
        event->key = Key::Home;
        return true;
    }
    if (first == 5) {
        event->key = Key::End;
        return true;
    }
    if (first == '\t') {
        event->key = Key::Tab;
        return true;
    }
    if (first == '\r' || first == '\n') {
        event->key = Key::Enter;
        return true;
    }
    if (first == 0x7f || first == 0x08) {
        event->key = Key::Backspace;
        return true;
    }
    if (first != 0x1b) {
        QByteArray bytes;
        bytes.append(static_cast<char>(first));
        int expected = 1;
        if ((first & 0xe0) == 0xc0)
            expected = 2;
        else if ((first & 0xf0) == 0xe0)
            expected = 3;
        else if ((first & 0xf8) == 0xf0)
            expected = 4;
        while (bytes.size() < expected) {
            unsigned char next = 0;
            if (::read(STDIN_FILENO, &next, 1) != 1)
                break;
            bytes.append(static_cast<char>(next));
        }
        const QString decoded = QString::fromUtf8(bytes);
        if (!decoded.isEmpty()) {
            event->key = Key::Character;
            event->character = decoded.at(0);
        }
        return true;
    }

    unsigned char seq[8] = {};
    if (::read(STDIN_FILENO, &seq[0], 1) != 1)
        return true;
    if (seq[0] == '[') {
        if (::read(STDIN_FILENO, &seq[1], 1) != 1)
            return true;
        switch (seq[1]) {
        case 'A':
            event->key = Key::Up;
            return true;
        case 'B':
            event->key = Key::Down;
            return true;
        case 'C':
            event->key = Key::Right;
            return true;
        case 'D':
            event->key = Key::Left;
            return true;
        case 'H':
            event->key = Key::Home;
            return true;
        case 'F':
            event->key = Key::End;
            return true;
        default:
            break;
        }
        if (seq[1] >= '0' && seq[1] <= '9') {
            if (::read(STDIN_FILENO, &seq[2], 1) != 1)
                return true;
            if (seq[2] == '~') {
                if (seq[1] == '3')
                    event->key = Key::Delete;
                else if (seq[1] == '1' || seq[1] == '7')
                    event->key = Key::Home;
                else if (seq[1] == '4' || seq[1] == '8')
                    event->key = Key::End;
            }
        }
        return true;
    }
    if (seq[0] == 'O') {
        if (::read(STDIN_FILENO, &seq[1], 1) != 1)
            return true;
        if (seq[1] == 'H')
            event->key = Key::Home;
        else if (seq[1] == 'F')
            event->key = Key::End;
    }
    return true;
#else
    Q_UNUSED(event);
    return false;
#endif
}

void CliLineEditor::insertText(const QString &text)
{
    if (text.isEmpty())
        return;
    m_buffer.insert(m_cursor, text);
    m_cursor += text.size();
    redraw();
}

void CliLineEditor::deleteBeforeCursor()
{
    if (m_cursor <= 0)
        return;
    m_buffer.remove(m_cursor - 1, 1);
    --m_cursor;
    redraw();
}

void CliLineEditor::deleteAtCursor()
{
    if (m_cursor >= m_buffer.size())
        return;
    m_buffer.remove(m_cursor, 1);
    redraw();
}

void CliLineEditor::moveLeft()
{
    if (m_cursor <= 0)
        return;
    --m_cursor;
    redraw();
}

void CliLineEditor::moveRight()
{
    if (m_cursor >= m_buffer.size())
        return;
    ++m_cursor;
    redraw();
}

void CliLineEditor::moveHome()
{
    if (m_cursor == 0)
        return;
    m_cursor = 0;
    redraw();
}

void CliLineEditor::moveEnd()
{
    if (m_cursor == m_buffer.size())
        return;
    m_cursor = m_buffer.size();
    redraw();
}

void CliLineEditor::historyUp()
{
    if (!m_history || m_history->count() == 0)
        return;
    if (!m_history->isNavigating())
        m_history->beginNavigation(m_buffer);
    m_buffer = m_history->moveUp();
    m_cursor = m_buffer.size();
    redraw();
}

void CliLineEditor::historyDown()
{
    if (!m_history || !m_history->isNavigating())
        return;
    m_buffer = m_history->moveDown();
    m_cursor = m_buffer.size();
    redraw();
}

void CliLineEditor::complete()
{
    if (!m_completionHandler)
        return;
    applyCompletion(m_completionHandler(m_buffer, m_cursor));
}

void CliLineEditor::applyCompletion(const CliCompletion::Result &completion)
{
    if (completion.values.isEmpty())
        return;

    const int from = qBound(0, completion.replaceFrom, m_buffer.size());
    const int to = qBound(from, completion.replaceTo, m_buffer.size());
    const QString current = m_buffer.mid(from, to - from);

    if (completion.values.size() == 1) {
        QString insertion = CliCompletion::quoteToken(completion.values.first());
        const QString after = m_buffer.mid(to);
        if (completion.appendSpace && (after.isEmpty() || !after.at(0).isSpace()))
            insertion.append(QLatin1Char(' '));
        m_buffer.replace(from, to - from, insertion);
        m_cursor = from + insertion.size();
        redraw();
        return;
    }

    const QString shared = CliCompletion::commonPrefix(completion.values);
    if (shared.size() > current.size()) {
        const QString insertion = shared;
        m_buffer.replace(from, to - from, insertion);
        m_cursor = from + insertion.size();
        redraw();
        return;
    }

    write(QStringLiteral("\n"));
    write(completion.values.join(QStringLiteral("  ")));
    write(QStringLiteral("\n"));
    redraw();
}

void CliLineEditor::redraw() const
{
    write(QStringLiteral("\r"));
    write(m_prompt);
    write(m_buffer);
    write(QStringLiteral("\x1b[K"));
    const int extra = m_buffer.size() - m_cursor;
    if (extra > 0)
        write(QStringLiteral("\x1b[%1D").arg(extra));
    flush();
}

void CliLineEditor::write(const QString &text) const
{
    if (!m_output)
        return;
    *m_output << text;
}

void CliLineEditor::flush() const
{
    if (!m_output)
        return;
    m_output->flush();
}
