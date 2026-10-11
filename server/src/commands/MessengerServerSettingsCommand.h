#ifndef MESSENGERSERVERSETTINGSCOMMAND_H
#define MESSENGERSERVERSETTINGSCOMMAND_H

#include "MessengerAbstractCommand.h"

class MessengerServerSettingsCommand final : public MessengerAbstractCommand
{
    Q_DECLARE_TR_FUNCTIONS(MessengerAbstractCommand)

public:
    explicit MessengerServerSettingsCommand(MessengerCommandManager *manager);

    QStringList details() const override;
    QStringList examples() const override;

protected:
    void configureParser(MessengerCommandParser &parser) const override;

public:
    void execute(const AbstractBot::IncomingMessage &message, const QString &args) override;
};

#endif // MESSENGERSERVERSETTINGSCOMMAND_H
