#ifndef MESSENGERSOONESTCOMMAND_H
#define MESSENGERSOONESTCOMMAND_H

#include "MessengerAbstractCommand.h"

class MessengerSoonestCommand : public MessengerAbstractCommand
{
    Q_DECLARE_TR_FUNCTIONS(MessengerSoonestCommand)

public:
    explicit MessengerSoonestCommand(MessengerCommandManager *manager);

    void execute(const AbstractBot::IncomingMessage &message, const QString &args) override;
    QStringList details() const override;
    QStringList examples() const override;

protected:
    void configureParser(MessengerCommandParser &parser) const override;
};

BOOKING_RESOURCES_REGISTER_COMMAND(MessengerSoonestCommand)

#endif // MESSENGERSOONESTCOMMAND_H
