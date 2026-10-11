#ifndef WEBAPPLICATION_H
#define WEBAPPLICATION_H

#include "BookingResourcesApplication.h"

class WebServer;
class WebClient;
class WebAuthManager;
class WebSessionStore;
class WebLocalCliCommands;
class LocalCliEndpoint;
class BookingResourcesTranslator;

class WebApplication final : public BookingResourcesApplication
{
public:
    WebApplication(int &argc, char **argv);
    ~WebApplication() override;

    enum class ExitCode {
        Success = 0,
        CommandLineError = 1,
        StorageInitFailed = 2,
        WebIpcInitFailed = 3,
        WebServerInitFailed = 4,
        LocalCliInitFailed = 5,
    };

    ExitCode tryStart();

protected:
    void performShutdown() override;

private:
    WebSessionStore *m_store;
    WebClient *m_webClient;
    WebAuthManager *m_authManager;
    WebServer *m_server;
    WebLocalCliCommands *m_localCliCommands;
    LocalCliEndpoint *m_localCli;
    BookingResourcesTranslator *m_translator;
};

#endif // WEBAPPLICATION_H
