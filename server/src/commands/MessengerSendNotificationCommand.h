#ifndef MESSENGER_SEND_NOTIFICATION_COMMAND_H
#define MESSENGER_SEND_NOTIFICATION_COMMAND_H

#include "MessengerAbstractCommand.h"

class MessengerSendNotificationCommand : public MessengerAbstractCommand
{
    Q_DECLARE_TR_FUNCTIONS(MessengerSendNotificationCommand)

public:
    explicit MessengerSendNotificationCommand(MessengerCommandManager *manager);

    void configureParser(MessengerCommandParser &parser) const override;
    void execute(const AbstractBot::IncomingMessage &message, const QString &args) override;
    QStringList details() const override;
    QStringList examples() const override;
};

BOOKING_RESOURCES_REGISTER_COMMAND(MessengerSendNotificationCommand)

#endif // MESSENGER_SEND_NOTIFICATION_COMMAND_H
