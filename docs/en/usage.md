# Usage Guide

## Table of Contents
- [User Commands](#user-commands)
- [Admin Commands](#admin-commands)
- [Interactive Menus](#interactive-menus)
- [Local CLI Usage](#local-cli-usage)
- [Web Interface](#web-interface)

Users with active access interact through slash commands, inline menus, or the web chat. Slash commands are text commands that start with `/`. Inline menus are bot messages with attached buttons, when the backend supports them. Administrators use the same command layer from messenger backends or [Local CLI](applications.md#booking-resources-cli).

Command arguments are whitespace-separated. Use quotes for most values with spaces:

```text
/add_resource "Meeting room" "Room with projector" "Return the key after use"
```

In command descriptions, `<argument>` means a required argument, `[argument]` means an optional argument, and `[argument...]` means an optional argument that can contain multiple values or the rest of the command text.

One message can contain multiple slash commands on separate lines. A line break starts a new command only when it is outside quotes and not escaped with a preceding backslash. Quoted values and backslash-continued text can therefore contain real line breaks.

```text
/help
/list_resources
/add_resource "Meeting room" "Room with projector" "Return the key
after use"
```

Duration arguments accept plain minutes or a number with a unit. English units are always accepted: `m`, `min`, `minute`, `minutes`; `h`, `hr`, `hour`, `hours`; `d`, `day`, `days`; `w`, `week`, `weeks`; `mo`, `mon`, `month`, `months`. Localized unit names are accepted for the active locale, for example `5 минут` in Russian. For commands that store durations in minutes, one month is treated as 30 days.

## User Commands

| Command | Arguments | Description |
| --- | --- | --- |
| <a id="cmd-start"></a>`/start` | none | Show the welcome/start message. |
| <a id="cmd-help"></a>`/help` | `[command]` | Show available bot commands and detailed command usage. |
| <a id="cmd-menu"></a>`/menu` | none | Open the interactive menu if the backend supports inline keyboards. |
| <a id="cmd-free-resources"></a>`/free_resources` | none | List resources that are currently free. |
| <a id="cmd-list-resources"></a>`/list_resources` | none | List all visible resources except disabled ones. |
| <a id="cmd-soonest"></a>`/soonest` | `[count]` | Show resources that free up soon. Default count is `5`, capped at `20`. A non-numeric or non-positive count is rejected. |
| <a id="cmd-queue"></a>`/queue` | `<name>` | Show queue and schedule for a resource. |
| <a id="cmd-list-users"></a>`/list_users` | none | List users inside the viewer's group scope. Administrators see full details only for users they can administer. |
| <a id="cmd-occupy"></a>`/occupy` | `<name> [duration...]` | Occupy a resource, or wait for release when it is already occupied. If duration is omitted, the bot offers allowed durations. `0` means indefinite when enabled. |
| <a id="cmd-release"></a>`/release` | `<name>` | Release a resource occupied by the current user. |
| <a id="cmd-release-all"></a>`/release_all` | none | Release all resources currently occupied by the current user. Waiting-queue and scheduled reservations are not removed. |
| <a id="cmd-schedule"></a>`/schedule` | `<name> <start> [end...]` | Schedule a resource for a time window. Empty end means indefinite. Date/time follows `ui.date_time_format`; ISO input is also accepted. Use `0` as start to wait for release; then `[end]` is a duration. The readable start value `"when the resource is released"` is also accepted. |
| <a id="cmd-unschedule"></a>`/unschedule` | `<name>` | Remove the current user's schedule for a resource. |
| <a id="cmd-user-settings"></a>`/user_settings` | `<action> [key] [value...]` | List, read, or update personal settings. |
| <a id="cmd-statistics"></a>`/statistics` | `<resources> <users> <start> <end|now> [format] [report_kind]` | Build a statistics report. If statistics are disabled, the command and [`/help`](#cmd-help) `/statistics` reply that they are not available. If `stats.admins_only` is set, non-admins receive an access-denied reply. |

User settings:

| Key | Values |
| --- | --- |
| `locale` | `default`, `auto`, `messenger`, or a locale such as `en`, `ru`, or `ru_stand`. In the web UI, `default` follows the browser language and resolves to `en` or `ru`. |
| `timezone` | `default`, `auto`, `system`, `UTC+03:00`, `+03:00`, `03:00`, `-03:00`, or minutes such as `180`. |
| `occupation.notify` | `all`, `mine`, `off`; aliases: `all_resources`, `own`, `own_resources`, `disabled`. `mine` sends notifications only about occupation changes involving the current user. |

`ru_stand` is the specialized Russian locale that keeps Russian UI text but renders `resource` / `resources` as `stand` / `stands`.

Examples:

```text
/occupy Printer 5 min
/occupy Printer 5 minutes
/occupy Printer 2 hours
/schedule Printer "2026-02-03 10:00" "2026-02-03 12:00"
/schedule Printer 0 5 min
/schedule Printer 0 5 minutes
/schedule Printer 0 2 hours
/user_settings set locale ru
/user_settings set locale ru_stand
/user_settings set timezone UTC+03:00
```

[`/statistics`](#cmd-statistics) accepts `csv`, `table`, or `all` as the optional output format; default is `csv`. Resource and user filters are comma-separated lists, while `all`, `-`, or `none` include every object authorized for the caller, never objects from sibling or parent groups. Scoped administrators receive only fully administered resources/users; ordinary users receive their own user activity and visible resources. Legacy statistics rows without a stored group snapshot are available only to server/root administrators. Optional `report_kind` values are `utilization`, `user_activity`, `top_resources`, `peak_hours`, `plan_vs_actual`, `weekday_pattern`, `duration_distribution`, `initiators`, and `events`; aliases include `utilization_summary`, `user_activity_summary`, `top`, `peak`, `scheduled_vs_actual`, `session_duration_distribution`, `initiator_analysis`, `event_log`, and `events_log`. Start and end times use the current `ui.date_time_format`; invalid values name the expected format. The command stays hidden from `/help` when it is disabled or restricted, but typing [`/statistics`](#cmd-statistics) or [`/help`](#cmd-help) `/statistics` still returns that refusal instead of an unknown-command reply.

## Admin Commands

Runtime settings are server settings stored in SQLite and changed with [`/server_settings`](#cmd-server-settings).

| Command | Arguments | Description |
| --- | --- | --- |
| <a id="cmd-blacklist"></a>`/blacklist` | none | Show blacklisted users. |
| <a id="cmd-pending-access"></a>`/pending_access` | none | Show recently seen users awaiting access activation. Requires server-admin access because pending identities have no group yet. |
| <a id="cmd-server-settings"></a>`/server_settings` | `<action> [key] [value...]` | List/get/set runtime server settings. Requires server-admin access. |
| <a id="cmd-occupation-durations"></a>`/occupation_durations` | `<action> [duration...]` | List/add/remove allowed occupation durations. Requires server-admin access. `0` means indefinite. Unknown actions are rejected (`list`, `add`, `remove`). |
| <a id="cmd-send-notification"></a>`/send_notification` | `[recipients] [message...]` | Send an alert. Recipients must be fully inside groups administered by the sender; a comma-separated `<messenger:id>` list targets specific manageable users. Unparsed or unknown recipient keys are reported instead of being skipped. |
| <a id="cmd-add-resource"></a>`/add_resource` | `<name> <description> <start-message> [groups]` | Add a resource. Use `""` for empty description/start message. Omitted groups default to all administered group roots; explicit groups are comma-separated slash-separated paths such as `office/floor1`. |
| <a id="cmd-update-resource"></a>`/update_resource` | `<name> <description> <start-message> [status] [groups]` | Update a resource. Input statuses: `On`, `TemporarilyUnavailable`, `Off`. Optional groups use the same path rules as [`/add_resource`](#cmd-add-resource). |
| <a id="cmd-delete-resource"></a>`/delete_resource` | `<name>` | Delete a resource. |
| <a id="cmd-add-user"></a>`/add_user` | `<messenger:id> [first] [last] [admin] [groups]` | Activate access for a user. Omitted groups default to all administered group roots. First/last name can be omitted to use messenger profile values. `admin` accepts `1`/`0`, `true`/`false`, `yes`/`no`, `on`/`off`, or `admin`/`user`. |
| <a id="cmd-update-user"></a>`/update_user` | `<messenger:id> [first] [last] [admin] [groups]` | Update an existing user. First/last name can be omitted to keep existing values. `admin` uses the same tokens as [`/add_user`](#cmd-add-user); an invalid flag is rejected. |
| <a id="cmd-delete-user"></a>`/delete_user` | `<messenger:id>` | Delete a user. |
| <a id="cmd-ban-user"></a>`/ban_user` | `<messenger:id> <duration> [reason...]` | Block a user. Duration accepts minutes, hours, days, weeks, months, or `permanent`; `perm` and `forever` are also accepted. |
| <a id="cmd-unban-user"></a>`/unban_user` | `<messenger:id>` | Remove a user from the blacklist. |

Default occupation durations are `0`, `5`, `30`, `60`, `120`, and `240` minutes. `0` means indefinite and cannot be removed. Resource status input tokens are `On` (`on`), `TemporarilyUnavailable` (`temporarily_unavailable` or `temporarily-unavailable`), and `Off` (`off`).

Occupation duration examples: `/occupation_durations add 5 minutes`, `/occupation_durations add 2 hours`, `/occupation_durations remove 1 hour`.

For [`/ban_user`](#cmd-ban-user), a two-word duration can be quoted or left unquoted before the reason: `"5 minutes" spam` and `5 minutes spam` both mean a 5-minute block with reason `spam`.

Admin flag values for user commands: use `1`, `true`, `yes`, `y`, `on`, or `admin` to enable; use `0`, `false`, `no`, `n`, `off`, or `user` to disable. Group roles use `user` and `admin`:

```text
group=user,group/subgroup=admin
```

For scoped administrators, explicitly supplied groups must be equal to or nested below an
administered root. `-` or an explicitly empty group means `Root (all groups)` and is therefore
accepted only for a server/root administrator. Updating or deleting a multi-group user/resource
requires authority over every involved group. Blacklist operations follow the active target's
groups; only server/root administrators can block or unblock an unknown key.

Examples:

```text
/add_user telegram:1234 John Doe true
/add_user telegram:1234 "" "" admin
/update_user telegram:1234 admin
/add_resource "Printer" "Color laser printer" "Load paper before use" "office/floor1"
/ban_user telegram:1234 "5 minutes" "spam"
/ban_user telegram:1234 2 hours "spam"
/server_settings set menu.update_mode passive
```

## Interactive Menus

Open the menu with:

```text
/menu
```

`Main menu` contains:

- `Booking Resources`.
- `User list`, which opens `Users with active access`.
- `Statistics`, when enabled for the current user.
- `User settings`.
- `Administration`, for admins only.

`Booking Resources` contains:

- `Resource cards`.
- `Free resources`.
- `Releases soon`.
- `Release occupied resources`, when the current user is occupying at least two resources right now.
- `Resource overview` with `Occupy`, `Release`, `View queue`, and `Plan usage` actions where applicable.

`Releases soon` starts with `5` resources and offers the `Increase depth` button to expand the list to `10`.

`User settings` contains:

- `Language`.
- `Time zone`.
- `Occupation notifications`.

`Administration` contains:

- `Manage resources`.
- `Manage users`.
- `Users awaiting access activation`.
- `Send alert to users`.
- `User blacklist`.
- `Resource occupation durations`.
- `Server settings`, visible only to server admins.

Menus keep a per-chat/per-user session. By default, a menu session expires after 30 minutes of inactivity. In `active` update mode, open menus are refreshed after model changes; in `passive` mode, menus show explicit refresh buttons.

## Local CLI Usage

The [Local CLI](applications.md#booking-resources-cli) can attach to a running [`booking-resources-server`](applications.md#booking-resources-server) or [`booking-resources-web`](applications.md#booking-resources-web) process. Server connections forward slash commands as an admin-only local user. Web connections send web user-management commands to the web SQLite store; they do not call server [`/add_user`](#cmd-add-user). `Ctrl+C` disconnects the CLI from the current process and does not stop the server or web process.

Common commands:

```text
booking-resources-cli --list
booking-resources-cli --pid <server_pid>
booking-resources-cli --pid <server_pid> --command "/help"
booking-resources-cli --pid <server_pid> --command "/server_settings list"
booking-resources-cli --web-shared-memory-key <web_key>
booking-resources-cli --web-shared-memory-key <web_key> --command "/list_users"
```

Inside the interactive shell:

```text
\list
\connect <pid>
\connect web <web_key>
/help
/add_resource "Printer" "" ""
\quit
```

On a TTY, Up/Down walks previous commands from this CLI process and Tab completes meta-commands, slash-commands, and arguments. See [booking-resources-cli](applications.md#booking-resources-cli).

Only admin commands are accepted through a server [Local CLI](applications.md#booking-resources-cli) connection. Inspection commands [`/list_resources`](#cmd-list-resources), [`/list_users`](#cmd-list-users), [`/free_resources`](#cmd-free-resources), [`/soonest`](#cmd-soonest), and [`/queue`](#cmd-queue) are available there. User-only commands such as [`/occupy`](#cmd-occupy), [`/release`](#cmd-release), or [`/release_all`](#cmd-release-all) are rejected.

A web [Local CLI](applications.md#booking-resources-cli) connection accepts:

| Command | Description |
| --- | --- |
| `/help [command]` | Lists web Local CLI commands with a short description. With a command name, shows usage and extra details. |
| `/list_users` | Lists web users: login, `web:<uuid>`, name, locale, provider, and whether a password is set. |
| `/add_user <login> <first_name> [last_name] [locale]` | Creates a local login/password web user without a working password and prints a one-time `/set-password.html` link. Requires `log-in-only` or `log-in-with-registration`. |
| `/delete_user <login\|user_id\|web:uuid>` | Deletes the web-local user, credentials, sessions, events, files, command cache, and password tokens. The server `web:<uuid>` account is not changed. |
| `/reset_password <login\|user_id\|web:uuid> [login]` | Invalidates the current password and issues a one-time password link valid for 1 day. Existing local login/password users get `/reset-password.html`. A user without a login requires `[login]` and gets `/set-password.html`. Requires `log-in-only` or `log-in-with-registration`. |

Password links use `web/public_https_host` when it is set. Otherwise the CLI prints a localhost origin and notes that `web/public_https_host` is needed to share the link outside the machine.

## Web Interface

The web interface is served by [`booking-resources-web`](applications.md#booking-resources-web).

Pages:

| Page | Purpose |
| --- | --- |
| `/` or `/index.html` | Landing page with a link to chat. |
| `/authorize.html` | Creates or restores a web user id session using an enabled authorization method. |
| `/set-password.html` | Public page that sets the first password from a one-time `set_password` token. Shows the assigned login as read-only. |
| `/reset-password.html` | Public page that sets a new password from a one-time `reset_password` token. |
| `/chat.html` | Browser chat for slash commands, the command menu button, bot messages, files, inline keyboards, command suggestions, and the in-chat settings panel. In message text, `http(s)`, `www.`, and `mailto:` URLs become clickable links. |

Primary `sign-without-registration` flow:

1. Open `/authorize.html`.
2. Enter first name, optional last name, and locale.
3. The web process creates a new `web:<uuid>` user id and records it on the server as awaiting access activation.
4. Copy `web:<uuid>` from the pending-access notice in chat or from the settings panel, then provide it to an administrator.
5. Administrator activates access for that exact key with [`/add_user`](#cmd-add-user) `web:<uuid> ...` or through the admin menu.
6. Use `/chat.html` as the bot chat.

When [`booking-resources-web`](applications.md#booking-resources-web) enables `log-in-with-registration`, `/authorize.html` also offers local account registration and login. Registration still creates a generated `web:<uuid>` user id and records it on the server as awaiting access activation. `log-in-only` shows login only: an administrator creates the account through web [Local CLI](applications.md#booking-resources-cli) `/add_user` without a working password; that user opens `/set-password.html` from the printed link, then signs in on `/authorize.html`. The same Local CLI flow also works with `log-in-with-registration`. Later logins use the local login/password only to restore the same web user id and issue a new `br_session` cookie; server-side identity and permissions remain tied to `web:<uuid>`. A signed-in local login/password user can change the password from the chat settings panel; the login is fixed after it is assigned. `sign-without-registration` can be combined with one login method.

Authorization and settings forms expose `en`, `ru`, and `ru_stand`. `ru_stand` keeps Russian UI text but renders `resource` / `resources` as `stand` / `stands`.

For `log-in-with-registration`, registration validates `login` as 3-128 characters from letters, digits, `.`, `_`, `@`, `+`, and `-`. Passwords must contain at least 8 characters and must not contain control characters.

The web session is stored in the `br_session` cookie with `HttpOnly` and `SameSite=Lax`; it is marked `Secure` when HTTPS or external HTTPS is active. Chat history and command cache are stored locally in the browser and are scoped by web user id.

The button to the left of the message input opens the full slash-command list. Typing `/` still filters the command suggestions in place. Enter sends the current message. Shift+Enter inserts a line break. Multiple slash commands and quoted line breaks in the web chat follow the same rules as in the command section above.

The in-chat settings panel also includes browser-local theme selection and local history limit (`20`-`1000` messages), a clear-local-history action, and logout. Locale is stored web-locally through `POST /api/profile` and remains editable even before server access activation. `Default` follows the browser language (`navigator.language` / `Accept-Language`) and resolves to `en` or `ru`; `ru_stand` is used only when it is selected explicitly. The web UI first renders in the browser language (`en` or `ru`), then applies the saved profile or `/api/user-settings` locale. Time zone and occupation notifications are server-backed settings handled through `/api/user-settings`; these controls stay disabled until access is activated. The settings `OK` button stays enabled in that pending-access case so locale, theme, and the local history limit can still be saved. If user settings cannot be loaded because the web server, the server, or the settings themselves are unavailable, `OK` is disabled.

If the browser cannot reach [`booking-resources-web`](applications.md#booking-resources-web), the chat shows `Web server is not available.` and hides the message composer. If the web process is up but [`booking-resources-server`](applications.md#booking-resources-server) is not reachable over IPC, the chat shows `Server is not available.` instead. Authorization and server-backed settings show the same `Server is not available.` text when those actions require the server.

Public API endpoints:

| Endpoint | Description |
| --- | --- |
| `GET /health` | Public liveness endpoint that returns a compact JSON response. |
| `GET /api/runtime-config` | Returns the configured page title and enabled web authorization methods. |
| `GET /api/i18n?locale=<locale>` | Returns the web UI dictionary. |

Authenticated API endpoints:

| Endpoint | Description |
| --- | --- |
| `GET /api/session` | Returns current web session profile. |
| `POST /api/profile` | Updates the web-local locale for the current session/user. |
| `GET /api/commands?locale=<locale>` | Returns available command metadata and completions for the requested or current locale. |
| `GET /api/user-settings` | Reads server-backed user settings. |
| `POST /api/user-settings` | Updates server-backed user settings. |
| `POST /api/messages` | Sends a chat message to the server. |
| `POST /api/callbacks` | Sends an inline keyboard callback to the server. |
| `GET /api/events` | Returns `{ ok, events, server_available }` with pending bot events/messages for the web user. `server_available` is `false` while server IPC is down. Supports optional `wait=<ms>` long-polling, capped at `25000` ms; the wait is skipped while the server is unavailable. |
| `GET /api/files/<id>` | Downloads a file made available by the bot. |
| `POST /api/account/password` | Changes the local password for the current web user when `log-in-only` or `log-in-with-registration` is enabled. The login is not changed. |
| `POST /api/logout` | Revokes the current web session, if any, and clears the cookie. |

Authorization endpoints:

| Endpoint | Description |
| --- | --- |
| `POST /api/authorize` | Creates a web-local user/session through `sign-without-registration` and records the web user on the server as awaiting access activation. |
| `POST /api/register` | Creates a local login/password account with a generated web user id when `log-in-with-registration` is enabled. |
| `POST /api/login` | Verifies local login/password credentials and issues a web session for the existing web user id when `log-in-only` or `log-in-with-registration` is enabled. |
| `GET /api/account/password-token` | Returns login and purpose for a one-time password token. Public. |
| `POST /api/account/password-set` | Sets the first password from a `set_password` token. Public. Does not create a `br_session` cookie. |
| `POST /api/account/password-reset` | Sets a new password from a `reset_password` token. Public. Does not create a `br_session` cookie. |

`POST /api/authorize`, `POST /api/register`, `POST /api/login`, `GET /api/account/password-token`, `POST /api/account/password-set`, and `POST /api/account/password-reset` share rate limiting: `10` attempts per remote address per `60` seconds, with a global cap of `100` attempts per `60` seconds.
