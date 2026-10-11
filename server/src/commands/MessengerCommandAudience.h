#ifndef MESSENGERCOMMANDAUDIENCE_H
#define MESSENGERCOMMANDAUDIENCE_H

/**
 * @brief Defines who is allowed to execute a command.
 */
enum class MessengerCommandAudience {
    UsersOnly = 0,
    AdminsOnly,
    ServerAdminsOnly,
    AdminsAndUsers
};

#endif // MESSENGERCOMMANDAUDIENCE_H
