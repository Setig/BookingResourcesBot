#include "WebStaticTranslations.h"

QVector<WebStaticTranslations::Entry> WebStaticTranslations::entries()
{
    return {
        { "app_title", QT_TR_NOOP("Booking Resources") },
        { "loading", QT_TR_NOOP("Loading...") },
        { "go_to_chat", QT_TR_NOOP("Go to chat") },
        { "authorize_title", QT_TR_NOOP("Authorization") },
        { "authorize_intro", QT_TR_NOOP("Choose how to sign in.") },
        { "web_profile_title", QT_TR_NOOP("Sign in without registration") },
        { "local_password_title", QT_TR_NOOP("Account") },
        { "login", QT_TR_NOOP("Login") },
        { "password", QT_TR_NOOP("Password") },
        { "confirm_password", QT_TR_NOOP("Confirm password") },
        { "login_button", QT_TR_NOOP("Log in") },
        { "register_button", QT_TR_NOOP("Register") },
        { "login_mode_description",
          QT_TR_NOOP("Sign in with the login saved during registration to continue using your "
                     "existing web profile.") },
        { "registration_mode_description",
          QT_TR_NOOP("Create a login and password so you can keep access to this web profile and "
                     "sign in with this login later.") },
        { "web_profile_mode_description",
          QT_TR_NOOP("Temporary sign-in does not create a login. Access to this web profile may be "
                     "lost if the web session is lost.") },
        { "first_name", QT_TR_NOOP("First name") },
        { "last_name", QT_TR_NOOP("Last name") },
        { "locale", QT_TR_NOOP("Locale") },
        { "authorize_button", QT_TR_NOOP("Authorize") },
        { "current_user_authorized", QT_TR_NOOP("The current user is authorized.") },
        { "authorization_failed", QT_TR_NOOP("Authorization failed.") },
        { "access_activation_pending_notice",
          QT_TR_NOOP("Contact an administrator to request access activation.") },
        { "chat_title", QT_TR_NOOP("Chat") },
        { "message_placeholder", QT_TR_NOOP("Type a message...") },
        { "message_input_hint", QT_TR_NOOP("Enter to send, Shift+Enter for a new line") },
        { "send", QT_TR_NOOP("Send") },
        { "open_command_menu", QT_TR_NOOP("Open command menu") },
        { "settings_title", QT_TR_NOOP("Settings") },
        { "no_messages", QT_TR_NOOP("No messages yet.") },
        { "day_today", QT_TR_NOOP("Today") },
        { "day_yesterday", QT_TR_NOOP("Yesterday") },
        { "connection_lost", QT_TR_NOOP("Connection is not available.") },
        { "web_server_unavailable", QT_TR_NOOP("Web server is not available.") },
        { "server_unavailable", QT_TR_NOOP("Server is not available.") },
        { "theme", QT_TR_NOOP("Theme") },
        { "light_theme", QT_TR_NOOP("Light") },
        { "dark_theme", QT_TR_NOOP("Dark") },
        { "language", QT_TR_NOOP("Language") },
        { "default_language", QT_TR_NOOP("Default") },
        { "english", QT_TR_NOOP("English") },
        { "russian", QT_TR_NOOP("Russian") },
        { "russian_stand", QT_TR_NOOP("Russian (stands)") },
        { "timezone", QT_TR_NOOP("Time zone") },
        { "timezone_hint", QT_TR_NOOP("Select a UTC offset or use the server default.") },
        { "default_server_timezone", QT_TR_NOOP("Default (server timezone)") },
        { "occupation_notify", QT_TR_NOOP("Occupation notifications") },
        { "occupation_notify_tooltip",
          QT_TR_NOOP("Choose which occupation changes trigger notifications for you.") },
        { "occupation_notify_all", QT_TR_NOOP("All resources") },
        { "occupation_notify_all_tooltip",
          QT_TR_NOOP("Notify about occupation changes for all resources visible to you.") },
        { "occupation_notify_mine", QT_TR_NOOP("Only events involving me") },
        { "occupation_notify_mine_tooltip",
          QT_TR_NOOP("Notify only when you occupy or release a resource, or when a resource is "
                     "handed over to or from you.") },
        { "occupation_notify_off", QT_TR_NOOP("Disabled") },
        { "occupation_notify_off_tooltip", QT_TR_NOOP("Do not send occupation notifications.") },
        { "user_id", QT_TR_NOOP("Your User ID:") },
        { "copy_user_id", QT_TR_NOOP("Copy User ID to clipboard") },
        { "history_limit", QT_TR_NOOP("Local history limit") },
        { "clear_history", QT_TR_NOOP("Clear local history") },
        { "back_to_chat", QT_TR_NOOP("Back to chat") },
        { "ok", QT_TR_NOOP("OK") },
        { "cancel", QT_TR_NOOP("Cancel") },
        { "user_settings_load_failed", QT_TR_NOOP("User settings are not available.") },
        { "user_settings_save_failed", QT_TR_NOOP("Failed to save user settings.") },
        { "user_settings_access_activation_required",
          QT_TR_NOOP("Server user settings will be available after administrator access "
                     "activation.") },
        { "current_password", QT_TR_NOOP("Current password") },
        { "new_password", QT_TR_NOOP("New password") },
        { "confirm_new_password", QT_TR_NOOP("Confirm new password") },
        { "change_password", QT_TR_NOOP("Change password") },
        { "password_changed", QT_TR_NOOP("Password was changed.") },
        { "password_change_failed", QT_TR_NOOP("Failed to change password.") },
        { "passwords_do_not_match", QT_TR_NOOP("New passwords do not match.") },
        { "download_file", QT_TR_NOOP("Download file") },
        { "file_not_available", QT_TR_NOOP("File is not available.") },
        { "theme_toggle", QT_TR_NOOP("Toggle color theme") },
        { "open_settings", QT_TR_NOOP("Open settings") },
        { "logout", QT_TR_NOOP("Log out") },
        { "logout_hint",
          QT_TR_NOOP("End this web session. You can sign in again using an enabled "
                     "authorization method.") },
        { "delivery_status_pending", QT_TR_NOOP("Sending") },
        { "delivery_status_failed", QT_TR_NOOP("Not delivered") },
        { "delivery_status_delivered", QT_TR_NOOP("Delivered") },
        { "new_chat_message_title", QT_TR_NOOP("New message") },
        { "new_chat_message_body", QT_TR_NOOP("You have a new chat message.") },
        { "new_chat_file_message", QT_TR_NOOP("New file received.") },
        { "set_password_title", QT_TR_NOOP("Set password") },
        { "set_password_intro",
          QT_TR_NOOP("Choose a password for the login assigned by the administrator.") },
        { "set_password_button", QT_TR_NOOP("Set password") },
        { "reset_password_title", QT_TR_NOOP("Reset password") },
        { "reset_password_intro", QT_TR_NOOP("Choose a new password for your web account.") },
        { "reset_password_button", QT_TR_NOOP("Reset password") },
        { "go_to_sign_in", QT_TR_NOOP("Go to sign in") },
        { "password_link_invalid", QT_TR_NOOP("Password link is invalid or has expired.") },
        { "password_was_set", QT_TR_NOOP("Password was set. You can sign in now.") },
        { "password_was_reset", QT_TR_NOOP("Password was reset. You can sign in now.") },
        { "password_set_failed", QT_TR_NOOP("Failed to set password.") },
        { "password_reset_failed", QT_TR_NOOP("Failed to reset password.") },
    };
}

void WebStaticTranslations::touchTranslations()
{
    (void)tr("Booking Resources");
    (void)tr("Loading...");
    (void)tr("Go to chat");
    (void)tr("Authorization");
    (void)tr("Choose how to sign in.");
    (void)tr("Sign in without registration");
    (void)tr("Account");
    (void)tr("Login");
    (void)tr("Password");
    (void)tr("Confirm password");
    (void)tr("Log in");
    (void)tr("Register");
    (void)tr("Sign in with the login saved during registration to continue using your existing web "
             "profile.");
    (void)tr("Create a login and password so you can keep access to this web profile and sign in "
             "with this login later.");
    (void)tr("Temporary sign-in does not create a login. Access to this web profile may be lost if "
             "the web session is lost.");
    (void)tr("First name");
    (void)tr("Last name");
    (void)tr("Locale");
    (void)tr("Authorize");
    (void)tr("The current user is authorized.");
    (void)tr("Authorization failed.");
    (void)tr("Contact an administrator to request access activation.");
    (void)tr("Chat");
    (void)tr("Type a message...");
    (void)tr("Enter to send, Shift+Enter for a new line");
    (void)tr("Send");
    (void)tr("Open command menu");
    (void)tr("Settings");
    (void)tr("No messages yet.");
    (void)tr("Today");
    (void)tr("Yesterday");
    (void)tr("Connection is not available.");
    (void)tr("Web server is not available.");
    (void)tr("Server is not available.");
    (void)tr("Theme");
    (void)tr("Light");
    (void)tr("Dark");
    (void)tr("Language");
    (void)tr("Default");
    (void)tr("English");
    (void)tr("Russian");
    (void)tr("Russian (stands)");
    (void)tr("Time zone");
    (void)tr("Select a UTC offset or use the server default.");
    (void)tr("Default (server timezone)");
    (void)tr("Occupation notifications");
    (void)tr("Choose which occupation changes trigger notifications for you.");
    (void)tr("All resources");
    (void)tr("Notify about occupation changes for all resources visible to you.");
    (void)tr("Only events involving me");
    (void)tr("Notify only when you occupy or release a resource, or when a resource is handed "
             "over to or from you.");
    (void)tr("Disabled");
    (void)tr("Do not send occupation notifications.");
    (void)tr("Your User ID:");
    (void)tr("Copy User ID to clipboard");
    (void)tr("Local history limit");
    (void)tr("Clear local history");
    (void)tr("Back to chat");
    (void)tr("OK");
    (void)tr("Cancel");
    (void)tr("User settings are not available.");
    (void)tr("Failed to save user settings.");
    (void)tr("Server user settings will be available after administrator access activation.");
    (void)tr("Current password");
    (void)tr("New password");
    (void)tr("Confirm new password");
    (void)tr("Change password");
    (void)tr("Password was changed.");
    (void)tr("Failed to change password.");
    (void)tr("New passwords do not match.");
    (void)tr("Download file");
    (void)tr("File is not available.");
    (void)tr("Toggle color theme");
    (void)tr("Open settings");
    (void)tr("Log out");
    (void)tr("End this web session. You can sign in again using an enabled authorization method.");
    (void)tr("Sending");
    (void)tr("Not delivered");
    (void)tr("Delivered");
    (void)tr("New message");
    (void)tr("You have a new chat message.");
    (void)tr("New file received.");
    (void)tr("Set password");
    (void)tr("Choose a password for the login assigned by the administrator.");
    (void)tr("Reset password");
    (void)tr("Choose a new password for your web account.");
    (void)tr("Go to sign in");
    (void)tr("Password link is invalid or has expired.");
    (void)tr("Password was set. You can sign in now.");
    (void)tr("Password was reset. You can sign in now.");
    (void)tr("Failed to set password.");
    (void)tr("Failed to reset password.");
}
