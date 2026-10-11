#ifndef MESSENGERADDRESOURCECOMMAND_H
#define MESSENGERADDRESOURCECOMMAND_H

#include "MessengerAbstractCommand.h"

class MessengerAddResourceCommand : public MessengerAbstractCommand
{
    Q_DECLARE_TR_FUNCTIONS(MessengerAddResourceCommand)

public:
    explicit MessengerAddResourceCommand(MessengerCommandManager *manager);

    void execute(const AbstractBot::IncomingMessage &message, const QString &args) override;
    QStringList details() const override;
    QStringList examples() const override;

protected:
    void configureParser(MessengerCommandParser &parser) const override;
};

BOOKING_RESOURCES_REGISTER_COMMAND(MessengerAddResourceCommand)

#endif // MESSENGERADDRESOURCECOMMAND_H
