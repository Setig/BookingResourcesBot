#ifndef MESSENGERSCHEDULECOMMAND_H
#define MESSENGERSCHEDULECOMMAND_H

#include "MessengerAbstractCommand.h"

class MessengerScheduleCommand : public MessengerAbstractCommand
{
    Q_DECLARE_TR_FUNCTIONS(MessengerScheduleCommand)

public:
    explicit MessengerScheduleCommand(MessengerCommandManager *manager);

    void execute(const AbstractBot::IncomingMessage &message, const QString &args) override;
    QStringList details() const override;
    QStringList examples() const override;

protected:
    void configureParser(MessengerCommandParser &parser) const override;
};

BOOKING_RESOURCES_REGISTER_COMMAND(MessengerScheduleCommand)

#endif // MESSENGERSCHEDULECOMMAND_H
