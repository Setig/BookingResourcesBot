#ifndef MESSENGERUPDATERESOURCECOMMAND_H
#define MESSENGERUPDATERESOURCECOMMAND_H

#include "MessengerAbstractCommand.h"

class MessengerUpdateResourceCommand : public MessengerAbstractCommand
{
    Q_DECLARE_TR_FUNCTIONS(MessengerUpdateResourceCommand)

public:
    explicit MessengerUpdateResourceCommand(MessengerCommandManager *manager);

    void execute(const AbstractBot::IncomingMessage &message, const QString &args) override;
    QStringList details() const override;
    QStringList examples() const override;

protected:
    void configureParser(MessengerCommandParser &parser) const override;
};

BOOKING_RESOURCES_REGISTER_COMMAND(MessengerUpdateResourceCommand)

#endif // MESSENGERUPDATERESOURCECOMMAND_H
