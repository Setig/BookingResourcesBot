#ifndef MESSENGERRELEASEALLCOMMAND_H
#define MESSENGERRELEASEALLCOMMAND_H

#include "MessengerAbstractCommand.h"

class MessengerReleaseAllCommand : public MessengerAbstractCommand
{
    Q_DECLARE_TR_FUNCTIONS(MessengerReleaseAllCommand)

public:
    explicit MessengerReleaseAllCommand(MessengerCommandManager *manager);

    void execute(const AbstractBot::IncomingMessage &message, const QString &args) override;
    QStringList details() const override;
    QStringList examples() const override;
};

BOOKING_RESOURCES_REGISTER_COMMAND(MessengerReleaseAllCommand)

#endif // MESSENGERRELEASEALLCOMMAND_H
