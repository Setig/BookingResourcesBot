#ifndef BOOKINGRESOURCESAPPLICATION_H
#define BOOKINGRESOURCESAPPLICATION_H

#include <QCoreApplication>

#include <QString>
#include <QtGlobal>

class QEvent;
class QLoggingCategory;
class BookingResourcesApplicationPrivate;

/**
 * @brief Common QCoreApplication wrapper for Booking Resources executables.
 *
 * The class centralizes application metadata, graceful quit handling, and
 * platform-specific termination requests while keeping process-specific startup
 * and shutdown work in derived classes.
 */
class BookingResourcesApplication : public QCoreApplication
{
public:
    BookingResourcesApplication(int &argc,
                                char **argv,
                                const QString &applicationName,
                                const QString &applicationVersion,
                                const QString &applicationDisplayName,
                                const QLoggingCategory &loggingCategory);
    ~BookingResourcesApplication() override;

    /**
     * @brief Logs the common startup message once.
     */
    void logStartup();

    /**
     * @brief Performs process-specific shutdown once.
     */
    void shutdown();

    /**
     * @brief Suppresses Ctrl+C / QEvent::Quit while a Local CLI client is connected.
     *
     * On Windows the same Ctrl+C is delivered to every process on the console and
     * may also post QEvent::Quit after the CLI has already disconnected. Quit is
     * ignored while booking-resources-cli still shares the console. On Unix,
     * SIGINT follows the same Ctrl+C rule; SIGTERM and SIGHUP still quit.
     */
    static void retainConsoleInterruptSuppression();
    static void releaseConsoleInterruptSuppression();
    static void resetConsoleInterruptSuppression();
    [[nodiscard]] static int consoleInterruptSuppressionCount();
    [[nodiscard]] static bool shouldAcceptQuitEvent();
    [[nodiscard]] bool shutdownPerformed() const;

protected:
    bool event(QEvent *e) override;

    /**
     * @brief Performs process-specific cleanup.
     *
     * Derived classes implement their own resource shutdown here. The method is
     * called at most once by shutdown().
     */
    virtual void performShutdown();

    void logShutdownStarted();
    void logShutdownFinished(qint64 elapsedMs = -1);

    [[nodiscard]] bool startupLogged() const;
    [[nodiscard]] bool shutdownStartedLogged() const;
    [[nodiscard]] bool shutdownFinishedLogged() const;

private:
    BookingResourcesApplicationPrivate *d;
};

#endif // BOOKINGRESOURCESAPPLICATION_H
