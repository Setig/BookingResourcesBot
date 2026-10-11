#ifndef MESSENGERUSERSETTINGSCOMMAND_H
#define MESSENGERUSERSETTINGSCOMMAND_H

#include "MessengerAbstractCommand.h"

class MessengerUserSettingsCommand final : public MessengerAbstractCommand
{
    Q_DECLARE_TR_FUNCTIONS(MessengerAbstractCommand)

public:
    explicit MessengerUserSettingsCommand(MessengerCommandManager *manager);

    QStringList details() const override;
    QStringList examples() const override;

protected:
    void configureParser(MessengerCommandParser &parser) const override;

public:
    void execute(const AbstractBot::IncomingMessage &message, const QString &args) override;
};

#endif // MESSENGERUSERSETTINGSCOMMAND_H
