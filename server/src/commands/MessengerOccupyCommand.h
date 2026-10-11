#ifndef MESSENGEROCCUPYCOMMAND_H
#define MESSENGEROCCUPYCOMMAND_H

#include "Resource.h"
#include "UserTimeZone.h"
#include "ResourceQueueTypes.h"
#include "MessengerAbstractCommand.h"

class MessengerOccupyCommand : public MessengerAbstractCommand
{
    Q_DECLARE_TR_FUNCTIONS(MessengerOccupyCommand)

public:
    explicit MessengerOccupyCommand(MessengerCommandManager *manager);

    /**
     * @brief Builds the occupy success reply used by /occupy and the resource card menu.
     *
     * Immediate occupation includes the resource start message as start instructions.
     * A queued after-release wait does not include the start message.
     */
    static QString buildOccupyReplyText(const Resource &resource,
                                        const QString &resourceName,
                                        const ResourceQueueEntry &occupant,
                                        const MessengerUserKey &actor,
                                        int durationMinutes,
                                        const QString &dateTimeFormat,
                                        const UserTimeZone &timeZone);

    void execute(const AbstractBot::IncomingMessage &message, const QString &args) override;
    QStringList details() const override;
    QStringList examples() const override;

protected:
    void configureParser(MessengerCommandParser &parser) const override;
};

BOOKING_RESOURCES_REGISTER_COMMAND(MessengerOccupyCommand)

#endif // MESSENGEROCCUPYCOMMAND_H
