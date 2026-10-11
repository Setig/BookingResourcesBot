#ifndef MESSENGEROCCUPATIONDURATIONSCOMMAND_H
#define MESSENGEROCCUPATIONDURATIONSCOMMAND_H

#include "MessengerAbstractCommand.h"

class MessengerOccupationDurationsCommand final : public MessengerAbstractCommand
{
    Q_DECLARE_TR_FUNCTIONS(MessengerOccupationDurationsCommand)

public:
    explicit MessengerOccupationDurationsCommand(MessengerCommandManager *manager);

    void execute(const AbstractBot::IncomingMessage &message, const QString &args) override;
    QStringList details() const override;
    QStringList examples() const override;

protected:
    void configureParser(MessengerCommandParser &parser) const override;
};

BOOKING_RESOURCES_REGISTER_COMMAND(MessengerOccupationDurationsCommand)

#endif // MESSENGEROCCUPATIONDURATIONSCOMMAND_H
