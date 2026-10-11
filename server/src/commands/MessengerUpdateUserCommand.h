#ifndef MESSENGERUPDATEUSERCOMMAND_H
#define MESSENGERUPDATEUSERCOMMAND_H

#include "MessengerAbstractCommand.h"

class MessengerUpdateUserCommand : public MessengerAbstractCommand
{
    Q_DECLARE_TR_FUNCTIONS(MessengerUpdateUserCommand)

public:
    explicit MessengerUpdateUserCommand(MessengerCommandManager *manager);

    void execute(const AbstractBot::IncomingMessage &message, const QString &args) override;
    QStringList details() const override;
    QStringList examples() const override;

protected:
    void configureParser(MessengerCommandParser &parser) const override;
};

BOOKING_RESOURCES_REGISTER_COMMAND(MessengerUpdateUserCommand)

#endif // MESSENGERUPDATEUSERCOMMAND_H
