#ifndef MESSENGERUNSCHEDULECOMMAND_H
#define MESSENGERUNSCHEDULECOMMAND_H

#include "MessengerAbstractCommand.h"

class MessengerUnscheduleCommand : public MessengerAbstractCommand
{
    Q_DECLARE_TR_FUNCTIONS(MessengerUnscheduleCommand)

public:
    explicit MessengerUnscheduleCommand(MessengerCommandManager *manager);

    void execute(const AbstractBot::IncomingMessage &message, const QString &args) override;
    QStringList details() const override;
    QStringList examples() const override;

protected:
    void configureParser(MessengerCommandParser &parser) const override;
};

BOOKING_RESOURCES_REGISTER_COMMAND(MessengerUnscheduleCommand)

#endif // MESSENGERUNSCHEDULECOMMAND_H
