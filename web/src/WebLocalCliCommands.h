#ifndef WEBLOCALCLICOMMANDS_H
#define WEBLOCALCLICOMMANDS_H

#include <QCoreApplication>
#include <QString>
#include <QStringList>

class WebAuthManager;
class WebSessionStore;

/**
 * @brief Dispatches booking-resources-web Local CLI slash-commands.
 */
class WebLocalCliCommands
{
    Q_DECLARE_TR_FUNCTIONS(WebLocalCliCommands)

public:
    struct Result
    {
        bool ok = false;
        QString text;
    };

    /**
     * @brief Tab-completion result for a Local CLI input line.
     */
    struct Completion
    {
        int replaceFrom = 0;
        int replaceTo = 0;
        QStringList values;
        bool appendSpace = true;
    };

    WebLocalCliCommands(WebAuthManager *authManager, WebSessionStore *store);
    ~WebLocalCliCommands();

    void setAuthMethods(const QStringList &methods);
    void setPublicOrigin(const QString &origin);
    [[nodiscard]] Result execute(const QString &command) const;
    [[nodiscard]] Completion complete(const QString &text, int cursor) const;

private:
    Result help(const QStringList &args) const;
    Result listUsers() const;
    Result addUser(const QStringList &args) const;
    Result deleteUser(const QStringList &args) const;
    Result resetPassword(const QStringList &args) const;
    [[nodiscard]] bool localCredentialsEnabled() const;
    [[nodiscard]] QString passwordUrl(const QString &page, const QString &token) const;
    [[nodiscard]] QString originNote() const;
    [[nodiscard]] static QStringList tokenize(const QString &text);

    WebAuthManager *m_authManager;
    WebSessionStore *m_store;
    QStringList m_authMethods;
    QString m_publicOrigin;
};

#endif // WEBLOCALCLICOMMANDS_H
