#ifndef MESSENGERBLACKLISTCOMMAND_H
#define MESSENGERBLACKLISTCOMMAND_H

#include "MessengerAbstractCommand.h"

class MessengerBlacklistCommand : public MessengerAbstractCommand
{
    Q_DECLARE_TR_FUNCTIONS(MessengerBlacklistCommand)

public:
    explicit MessengerBlacklistCommand(MessengerCommandManager *manager);

    void execute(const AbstractBot::IncomingMessage &message, const QString &args) override;
    QStringList details() const override;
    QStringList examples() const override;
};

BOOKING_RESOURCES_REGISTER_COMMAND(MessengerBlacklistCommand)

#endif // MESSENGERBLACKLISTCOMMAND_H
