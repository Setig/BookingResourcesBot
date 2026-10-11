#ifndef MESSENGERDELETEUSERCOMMAND_H
#define MESSENGERDELETEUSERCOMMAND_H

#include "MessengerAbstractCommand.h"

class MessengerDeleteUserCommand : public MessengerAbstractCommand
{
    Q_DECLARE_TR_FUNCTIONS(MessengerDeleteUserCommand)

public:
    explicit MessengerDeleteUserCommand(MessengerCommandManager *manager);

    void execute(const AbstractBot::IncomingMessage &message, const QString &args) override;
    QStringList details() const override;
    QStringList examples() const override;

protected:
    void configureParser(MessengerCommandParser &parser) const override;
};

BOOKING_RESOURCES_REGISTER_COMMAND(MessengerDeleteUserCommand)

#endif // MESSENGERDELETEUSERCOMMAND_H
