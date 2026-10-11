#ifndef MESSENGERADDUSERCOMMAND_H
#define MESSENGERADDUSERCOMMAND_H

#include "MessengerAbstractCommand.h"

class MessengerAddUserCommand : public MessengerAbstractCommand
{
    Q_DECLARE_TR_FUNCTIONS(MessengerAddUserCommand)

public:
    explicit MessengerAddUserCommand(MessengerCommandManager *manager);

    void execute(const AbstractBot::IncomingMessage &message, const QString &args) override;
    QStringList details() const override;
    QStringList examples() const override;

protected:
    void configureParser(MessengerCommandParser &parser) const override;
};

BOOKING_RESOURCES_REGISTER_COMMAND(MessengerAddUserCommand)

#endif // MESSENGERADDUSERCOMMAND_H
