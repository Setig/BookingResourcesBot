#ifndef MESSENGERLISTRESOURCESCOMMAND_H
#define MESSENGERLISTRESOURCESCOMMAND_H

#include "MessengerAbstractCommand.h"

class MessengerListResourcesCommand : public MessengerAbstractCommand
{
    Q_DECLARE_TR_FUNCTIONS(MessengerListResourcesCommand)

public:
    explicit MessengerListResourcesCommand(MessengerCommandManager *manager);

    void execute(const AbstractBot::IncomingMessage &message, const QString &args) override;
    QStringList details() const override;
    QStringList examples() const override;
};

BOOKING_RESOURCES_REGISTER_COMMAND(MessengerListResourcesCommand)

#endif // MESSENGERLISTRESOURCESCOMMAND_H
