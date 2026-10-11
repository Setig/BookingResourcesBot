#ifndef MESSENGERHELPCOMMAND_H
#define MESSENGERHELPCOMMAND_H

#include "commands/MessengerAbstractCommand.h"

class MessengerHelpCommand : public MessengerAbstractCommand
{
    Q_DECLARE_TR_FUNCTIONS(MessengerHelpCommand)

public:
    explicit MessengerHelpCommand(MessengerCommandManager *manager);

    void execute(const AbstractBot::IncomingMessage &message, const QString &args) override;
    QStringList details() const override;
    QStringList examples() const override;

protected:
    void configureParser(MessengerCommandParser &parser) const override;
};

BOOKING_RESOURCES_REGISTER_COMMAND(MessengerHelpCommand)

#endif // MESSENGERHELPCOMMAND_H
