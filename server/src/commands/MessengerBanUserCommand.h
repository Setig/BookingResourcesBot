#ifndef MESSENGERBANUSERCOMMAND_H
#define MESSENGERBANUSERCOMMAND_H

#include "MessengerAbstractCommand.h"

class MessengerBanUserCommand : public MessengerAbstractCommand
{
    Q_DECLARE_TR_FUNCTIONS(MessengerBanUserCommand)

public:
    explicit MessengerBanUserCommand(MessengerCommandManager *manager);

    void execute(const AbstractBot::IncomingMessage &message, const QString &args) override;
    QStringList details() const override;
    QStringList examples() const override;

protected:
    void configureParser(MessengerCommandParser &parser) const override;
};

BOOKING_RESOURCES_REGISTER_COMMAND(MessengerBanUserCommand)

#endif // MESSENGERBANUSERCOMMAND_H
