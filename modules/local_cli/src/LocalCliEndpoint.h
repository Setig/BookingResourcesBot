#ifndef LOCALCLIENDPOINT_H
#define LOCALCLIENDPOINT_H

#include <functional>

#include <QObject>
#include <QString>

#include "LocalCliProtocol.h"
#include "LocalCliRegistry.h"

class LocalCliEndpointPrivate;

/**
 * @brief QLocalServer host that speaks LocalCliProtocol and advertises itself in the registry.
 *
 * The endpoint is process-agnostic: callers supply a command handler and optional registry
 * metadata such as kind and shared-memory key.
 */
class LocalCliEndpoint final : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief Result returned by a command handler.
     */
    struct CommandResult
    {
        bool ok = false;
        QString text;
    };

    using CommandHandler = std::function<CommandResult(const QString &command)>;
    using CompletionHandler =
            std::function<LocalCliProtocol::CompletionResult(const QString &text, int cursor)>;

    explicit LocalCliEndpoint(QObject *parent = nullptr);
    ~LocalCliEndpoint() override;

    void setEndpointName(const QString &name);
    void setKind(LocalCliRegistry::Kind kind);
    void setSharedMemoryKey(const QString &key);
    void setWelcomeInfo(const QString &text);
    void setCommandHandler(CommandHandler handler);
    void setCompletionHandler(CompletionHandler handler);

    [[nodiscard]] QString endpointName() const;
    [[nodiscard]] bool isRunning() const;

    bool start();
    void stop();

signals:
    void clientSessionStarted();
    void clientSessionFinished();

private:
    LocalCliEndpointPrivate *d;
};

#endif // LOCALCLIENDPOINT_H
