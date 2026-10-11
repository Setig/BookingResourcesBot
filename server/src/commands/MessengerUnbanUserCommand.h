#ifndef MESSENGERUNBANUSERCOMMAND_H
#define MESSENGERUNBANUSERCOMMAND_H

#include "MessengerAbstractCommand.h"

class MessengerUnbanUserCommand : public MessengerAbstractCommand
{
    Q_DECLARE_TR_FUNCTIONS(MessengerUnbanUserCommand)

public:
    explicit MessengerUnbanUserCommand(MessengerCommandManager *manager);

    void execute(const AbstractBot::IncomingMessage &message, const QString &args) override;
    QStringList details() const override;
    QStringList examples() const override;

protected:
    void configureParser(MessengerCommandParser &parser) const override;
};

BOOKING_RESOURCES_REGISTER_COMMAND(MessengerUnbanUserCommand)

#endif // MESSENGERUNBANUSERCOMMAND_H
