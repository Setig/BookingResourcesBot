#ifndef MESSENGERLISTPENDINGACCESSUSERSCOMMAND_H
#define MESSENGERLISTPENDINGACCESSUSERSCOMMAND_H

#include "MessengerAbstractCommand.h"

class MessengerListPendingAccessUsersCommand : public MessengerAbstractCommand
{
    Q_DECLARE_TR_FUNCTIONS(MessengerListPendingAccessUsersCommand)

public:
    explicit MessengerListPendingAccessUsersCommand(MessengerCommandManager *manager);

    void execute(const AbstractBot::IncomingMessage &message, const QString &args) override;
    QStringList details() const override;
    QStringList examples() const override;
};

BOOKING_RESOURCES_REGISTER_COMMAND(MessengerListPendingAccessUsersCommand)

#endif // MESSENGERLISTPENDINGACCESSUSERSCOMMAND_H
