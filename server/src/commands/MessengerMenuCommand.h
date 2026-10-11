#ifndef MESSENGERMENUCOMMAND_H
#define MESSENGERMENUCOMMAND_H

#include "MessengerAbstractCommand.h"

#include "MessengerBot.h"
#include "MessengerCommandManager.h"

class MessengerMenuCommand : public MessengerAbstractCommand
{
    Q_DECLARE_TR_FUNCTIONS(MessengerMenuCommand)

public:
    explicit MessengerMenuCommand(MessengerCommandManager *manager);

    void execute(const AbstractBot::IncomingMessage &message, const QString &args) override;
    QStringList details() const override;
    QStringList examples() const override;
};

BOOKING_RESOURCES_REGISTER_COMMAND_IF(MessengerMenuCommand,
                                      manager && manager->bot() && manager->bot()->menuManager())

#endif // MESSENGERMENUCOMMAND_H
