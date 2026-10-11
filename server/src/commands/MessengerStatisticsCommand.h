#ifndef MESSENGERSTATISTICSCOMMAND_H
#define MESSENGERSTATISTICSCOMMAND_H

#include "MessengerAbstractCommand.h"

class MessengerStatisticsCommand final : public MessengerAbstractCommand
{
    Q_DECLARE_TR_FUNCTIONS(MessengerStatisticsCommand)

public:
    explicit MessengerStatisticsCommand(MessengerCommandManager *manager);

    bool isEnabled(const AbstractBot::IncomingMessage *message = nullptr) const override;
    QString disabledUserFacingReason(const AbstractBot::IncomingMessage &message) const override;

    void execute(const AbstractBot::IncomingMessage &message, const QString &args) override;
    QStringList details() const override;
    QStringList examples() const override;

protected:
    void configureParser(MessengerCommandParser &parser) const override;
};

#endif // MESSENGERSTATISTICSCOMMAND_H
