# Applications

## Table of Contents
- [`booking-resources-server`](#booking-resources-server)
- [`booking-resources-cli`](#booking-resources-cli)
- [`booking-resources-web`](#booking-resources-web)

Booking Resources Bot consists of three executables. All use Qt translations and standard `--help` / `--version` options.

A backend is a transport that connects the shared command layer to Telegram, Kontur.Talk, Web, or [Local CLI](#booking-resources-cli). Web IPC is the authenticated shared-memory channel between [`booking-resources-web`](#booking-resources-web) and [`booking-resources-server`](#booking-resources-server); both sides must use the same shared-memory key and secret.

Common environment variables:

| Variable | Values | Description |
| --- | --- | --- |
| `BOOKING_BOT_LOCALE` | locale code such as `en`, `ru`, `ru_stand`, `ru_RU`, or `ru-stand` | Used by default locale resolution when a process initializes `BookingResourcesTranslator` without an explicit preferred locale. This affects [`booking-resources-server`](#booking-resources-server) and [`booking-resources-cli`](#booking-resources-cli) startup; [`booking-resources-web`](#booking-resources-web) keeps its base translator in English and uses request-scoped locales for dictionaries and localized HTTP responses. Web [Local CLI](#booking-resources-cli) welcome text and command output use the same default locale resolution (`BOOKING_BOT_LOCALE`, then the system locale). Fallback goes from exact/normalized locale to Qt full locale, then language-only locale, then built-in English strings. |
| `QT_LOGGING_RULES` | standard Qt logging-category rules | Appends custom Qt logging filters to the shared default rule set, which starts with `*.debug=false`. |

Packaged deployments split into `booking-resources-bot-common`, `booking-resources-bot-server`, `booking-resources-bot-web`, and `booking-resources-bot-cli`. Linux packages install `booking-resources-bot-server.service` and `booking-resources-bot-web.service`, while Windows packages ship WinSW service templates for `BookingResourcesBotServer` and `BookingResourcesBotWeb`. Linux `.deb` packages for Ubuntu 24.04/26.04 and Qt 5 or Qt 6 can be built with Docker; see [Building Linux packages](building.md).

## `booking-resources-server`

The server is the main process. It opens the SQLite database, initializes runtime settings, creates messenger backends, records users/resources/queues/statistics, and hosts the [Local CLI](#booking-resources-cli) endpoint. It must start at least one real backend: Telegram, Kontur.Talk, or Web IPC. The [Local CLI](#booking-resources-cli) endpoint is created automatically, but it is not counted as a backend for startup validation. While a Local CLI client is connected, `Ctrl+C` and Qt quit events do not stop the server. Closing the console window, logoff, and shutdown still terminate it. On Linux, `SIGINT` follows the same `Ctrl+C` rule; `SIGTERM` and `SIGHUP` still stop the process.

Command-line options:

| Option | Value | Description |
| --- | --- | --- |
| `--config-file` | `file` | Use an explicit INI configuration file. It overrides both Qt scope files for reads and becomes the writable target for settings updates. |
| `-l`, `--log-file` | `file` | Base path for redirected Qt logs. Without it, logs are written to the console. |
| `--db-file` | `file` | SQLite database file. Overrides `database/file`. |
| `--web-shared-memory-key` | `key` | Enables the Web IPC backend and sets the shared-memory endpoint name. |
| `--web-shared-memory-secret` | `secret` | Shared secret for authenticated Web IPC frames. Required when Web IPC is enabled. |
| `--telegram-token` | `token` | Telegram bot API token. Overrides `bots/telegram/token`. |
| `--kontur-talk-token` | `token` | Kontur.Talk bot JWT token. Overrides `bots/kontur-talk/token`. |
| `--admin` | `messenger:user_id` | Ensures a fixed administrator account for the current server run. Can be repeated. |

Configuration is read from layered Qt INI settings. The server uses `BookingResourcesBot-Server` and `config` as the settings organization/application names. Resolution order is: dedicated CLI options such as `--db-file` or `--telegram-token` -> `--config-file` -> `BOOKING_RESOURCES_BOT_SERVER_CONFIG_FILE` -> Qt `UserScope` -> Qt `SystemScope` -> built-in defaults. When `--config-file` or `BOOKING_RESOURCES_BOT_SERVER_CONFIG_FILE` is used, that explicit file becomes the writable target; otherwise writes go to the Qt `UserScope` file and `SystemScope` stays read-only. Supported keys:

| Key | Description |
| --- | --- |
| `database/file` | SQLite database file. If empty, the server uses the application data location and `resources_bot.sqlite`, with a current-directory fallback. |
| `log/file` | Base path for log files. |
| `web/shared_memory_key` | Web IPC shared-memory key. |
| `web/shared_memory_secret` | Web IPC shared secret. |
| `bots/telegram/token` | Telegram bot token. Legacy key `telegram/token` is also read. |
| `bots/kontur-talk/token` | Kontur.Talk bot token. |
| `bots/<messenger>/admin_ids` | List of fixed admin user ids for a messenger during the current server run. Legacy key `telegram/admin_ids` is also read. |

Server configuration file locations:

| Source | Linux/Unix path | Windows path | Notes |
| --- | --- | --- | --- |
| `--config-file <file>` | exact file path | exact file path | Highest-priority explicit INI file. |
| `BOOKING_RESOURCES_BOT_SERVER_CONFIG_FILE` | exact file path | exact file path | Used when `--config-file` is not set. |
| Qt `UserScope` | `$XDG_CONFIG_HOME/BookingResourcesBot-Server/config.ini`, or `~/.config/BookingResourcesBot-Server/config.ini` when `XDG_CONFIG_HOME` is not set | `%APPDATA%\BookingResourcesBot-Server\config.ini` | Default writable target when no explicit file is selected. |
| Qt `SystemScope` | first matching `$XDG_CONFIG_DIRS/BookingResourcesBot-Server/config.ini`, with `/etc/xdg/BookingResourcesBot-Server/config.ini` as the default fallback location | `%ProgramData%\BookingResourcesBot-Server\config.ini` | Read-only fallback via `QSettings::SystemScope`. |

Create the parent directory manually when editing an explicit config file before the first run. Packaged Linux services use Qt `SystemScope` by default, so the installed `booking-resources-bot-server.service` reads `/etc/xdg/BookingResourcesBot-Server/config.ini` without passing `--config-file`.

Environment variable:

| Variable | Values | Description |
| --- | --- | --- |
| `BOOKING_RESOURCES_BOT_SERVER_CONFIG_FILE` | absolute or relative file path | Path to an explicit INI config file used when `--config-file` is not set. |

Complete `config.ini` example:

```ini
[database]
file=/var/lib/booking-resources-bot/resources_bot.sqlite

[log]
file=/var/log/booking-resources-bot/server.log

[web]
shared_memory_key=booking-resources-web
shared_memory_secret=change-me-long-random-secret

[bots/telegram]
token=123456789:telegram-bot-token
admin_ids=123456789, 987654321

[bots/kontur-talk]
token=kontur-talk-jwt-token
admin_ids=employee-1, employee-2

[bots/web]
admin_ids=web-user-id-1, web-user-id-2
```

`admin_ids` uses Qt INI list syntax; if a hand-written list contains only one id, keep it list-shaped with a trailing comma, for example `admin_ids=123456789,`. Supported messenger names are `telegram`, `kontur-talk`, `web`, and `local-cli`; `local-cli` user ids are client process ids, so they are normally not stored in persistent configuration.

Runtime settings are stored in SQLite and can be changed by server admins through [`/server_settings`](usage.md#cmd-server-settings) (`list`, `get`, `set`). Numeric settings accept non-negative integers; `0`, `unlimited`, and `disabled` map to no limit or disabled behavior where applicable.

| Key | Default | Description |
| --- | --- | --- |
| `queue.max_resources_per_user` | `0` | Maximum different resources one user may occupy or reserve at the same time. `0` means unlimited. |
| `queue.max_occupation_duration_minutes` | `0` | Maximum occupation duration in minutes. `0` means unlimited. |
| `menu.update_mode` | `active` | `active` auto-refreshes open menus; `passive` requires manual refresh buttons. |
| `menu.session_timeout_minutes` | `30` | Menu inactivity timeout. `0` disables timeout cleanup. |
| `stats.enabled` | `disabled` | Enables statistics collection. Accepts `enabled`/`disabled`, `on`/`off`, `true`/`false`, or `1`/`0`. When disabled, [`/statistics`](usage.md#cmd-statistics) and [`/help`](usage.md#cmd-help) `/statistics` reply that statistics are not available instead of treating the command as unknown. |
| `stats.admins_only` | `admins_only` | `admins_only` restricts [`/statistics`](usage.md#cmd-statistics) to admins; `all_users` allows users with active access. Non-admins still receive an access-denied reply if they type the command or [`/help`](usage.md#cmd-help) `/statistics`. |
| `ui.date_time_format` | `auto` | Date/time display and input format. `auto`/`default` uses the localized built-in format. |
| `access.pending_access_user_reply` | `default` | Reply text shown to users awaiting access activation. `auto`/`default` restores the localized built-in message. |

Default occupation durations are `0`, `5`, `30`, `60`, `120`, and `240` minutes. `0` means indefinite and cannot be removed.

The server initializes the database in WAL mode, runs `PRAGMA quick_check` during startup, creates a backup through `VACUUM INTO <db>.bak`, and renames corrupted databases to `*.corrupted.<timestamp>`.

## `booking-resources-cli`

The [Local CLI](#booking-resources-cli) is an administrative shell for a running [`booking-resources-server`](#booking-resources-server) or [`booking-resources-web`](#booking-resources-web) process. It connects over the local IPC endpoint registered by that process. Server connections forward slash commands as `Local CLI` admin messages. Web connections send web user-management commands to the web process. The CLI does not take `--web-shared-memory-secret`; the Local CLI channel is a trusted local socket, the same as the server Local CLI.

Command-line options:

| Option | Value | Description |
| --- | --- | --- |
| `-l`, `--list` | none | List running server and web Local CLI processes, including kind and web shared-memory key, and remove stale registry entries. |
| `-p`, `--pid` | `pid` | PID of the server or web process to connect to. |
| positional `pid` | `pid` | Alternative way to specify the process PID. |
| `--web-shared-memory-key` | `key` | Shared-memory key of a [`booking-resources-web`](#booking-resources-web) process. Selects that web Local CLI instance. |
| `-c`, `--command` | `command` | Execute one command and exit. Requires `--pid`, a positional PID, or `--web-shared-memory-key`. |

Interactive meta-commands:

| Command | Description |
| --- | --- |
| `\help` | Show CLI help. |
| `\connect <pid>` | Connect to a Local CLI process by PID. |
| `\connect web <key>` | Connect to [`booking-resources-web`](#booking-resources-web) by shared-memory key. |
| `\disconnect` | Disconnect from the current process. |
| `\list` | List running Local CLI processes. |
| `\quit`, `\exit`, `\q` | Disconnect and exit. |

On a real TTY the interactive shell supports Up/Down for previous commands and Tab completion for meta-commands, slash-commands, and arguments. Command history stays in memory for the current process and is discarded when the CLI exits. `--command` and piped stdin keep line-based input without history or Tab.

A web connection uses a prompt such as `booking-resources-cli[web:booking-resources-web]>`.

Environment variable:

| Variable | Values | Description |
| --- | --- | --- |
| `BOOKING_RESOURCES_CLI_COLOR` | `auto`, `always`, `on`, `1`, `true`, `never`, `off`, `0`, `false` | Controls prompt highlighting. Default is `auto`. |

## `booking-resources-web`

The web process serves the embedded HTML/CSS/JS interface and communicates with the server over authenticated shared-memory IPC. It has its own SQLite storage for web users, sessions, events, files, and cached command metadata. It also hosts a [Local CLI](#booking-resources-cli) endpoint. [`booking-resources-cli`](#booking-resources-cli) finds that endpoint by `--web-shared-memory-key` (the same key as Web IPC) or by PID. The Local CLI handshake is `hello` / `hello-ack` and does not use `web-shared-memory-secret`. While a Local CLI client is connected, `Ctrl+C` and Qt quit events do not stop the web process. Closing the console window, logoff, and shutdown still terminate it. On Linux, `SIGINT` follows the same `Ctrl+C` rule; `SIGTERM` and `SIGHUP` still stop the process.

HTTP and HTTPS listeners start even when [`booking-resources-server`](#booking-resources-server) is not running yet. The web process attaches to the shared-memory segments and verifies a `ping`/`pong` handshake in the background, then retries every few seconds until the server is available and again after the IPC connection is lost. Server-backed API routes return `503` with `error_code` `server_unavailable` while IPC is down. `GET /api/events` stays available and includes `server_available` so the chat UI can show `Server is not available.` The browser shows `Web server is not available.` only when it cannot reach the web process. A missing `web/shared_memory_secret` is still a startup error.

Command-line options:

| Option | Value | Description |
| --- | --- | --- |
| `--config-file` | `file` | Use an explicit INI configuration file. It overrides both Qt scope files for reads and becomes the writable target for settings updates. |
| `--bind-address` | `address` | HTTP and HTTPS bind address. |
| `--http-port` | `port` | HTTP listen port. Defaults to `8080`. |
| `--https-port` | `port` | HTTPS listen port. Defaults to `8443` when HTTPS is enabled. Use `443` for direct public HTTPS. |
| `--no-http` | none | Disable the HTTP listener. |
| `--redirect-http-to-https` | none | Redirect HTTP requests to HTTPS when HTTPS is enabled. |
| `--web-shared-memory-key` | `key` | Shared-memory key used to reach the server. |
| `--web-shared-memory-secret` | `secret` | Shared secret for authenticated IPC frames. Required. |
| `-l`, `--log-file` | `file` | Base path for redirected Qt logs. |
| `--https` | none | Enable HTTPS. |
| `--no-https` | none | Disable HTTPS even if configured. |
| `--external-https` | none | Treat requests as externally served over HTTPS, for example behind a reverse proxy. |
| `--allow-insecure-http` | none | Allow plain HTTP on non-loopback bind addresses. |
| `--forwarded-headers` | none | Trust `Forwarded` and `X-Forwarded-*` headers from loopback or configured trusted proxies. |
| `--trusted-proxy` | `address` | Trusted proxy address or CIDR. Can be repeated. Loopback proxies are trusted when forwarded headers are enabled. |
| `--public-https-host` | `host` | Public HTTPS host used in HTTP-to-HTTPS redirects and in Local CLI password set/reset links. |
| `--tls-certificate-file` | `file` | PEM certificate chain file. |
| `--tls-private-key-file` | `file` | PEM private key file. |
| `--tls-reload-interval` | `seconds` | Periodically reload TLS certificate and private key for future connections. `0` disables reload. |
| `--threads` | `count` | Maximum request worker thread count. Minimum is `1`. |
| `--storage-file` | `file` | SQLite storage file for the web process. |
| `--web-page-title` | `title` | Base title for web pages. |
| `--web-auth-method` | `method` | Enabled web authorization method. Supported values are `sign-without-registration`, `log-in-only`, and `log-in-with-registration`. Can be repeated or comma-separated. Defaults to `sign-without-registration`. Legacy aliases `web-profile` and `local-password` are accepted. |

Configuration is read from layered Qt INI settings. The web process uses `BookingResourcesBot-Web` and `web` as the settings organization/application names. Resolution order is: dedicated CLI options such as `--storage-file` or `--https` -> `--config-file` -> `BOOKING_RESOURCES_BOT_WEB_CONFIG_FILE` -> Qt `UserScope` -> Qt `SystemScope` -> built-in defaults. When `--config-file` or `BOOKING_RESOURCES_BOT_WEB_CONFIG_FILE` is used, that explicit file becomes the writable target; otherwise writes go to the Qt `UserScope` file and `SystemScope` stays read-only. Supported keys:

| Key | Default | Description |
| --- | --- | --- |
| `web/bind_address` | `127.0.0.1` | Bind address. |
| `web/http_port` | `8080` | HTTP listen port. |
| `web/https_port` | `8443` | HTTPS listen port when HTTPS is enabled. |
| `web/http_enabled` | `true` | Enables the HTTP listener. |
| `web/redirect_http_to_https` | `true` | Redirects HTTP requests to HTTPS when HTTPS is enabled. |
| `web/shared_memory_key` | `booking-resources-web` | IPC key. Must match the server key. |
| `web/shared_memory_secret` | empty | IPC secret. Must match the server secret. |
| `log/file` | empty | Base path for log files. |
| `web/https_enabled` | `false` | Enables HTTPS when certificate and private key are configured. |
| `web/external_https_enabled` | `false` | Enables secure-cookie/HSTS behavior for externally terminated HTTPS. |
| `web/allow_insecure_http` | `false` | Allows plain HTTP on non-loopback addresses. |
| `web/forwarded_headers_enabled` | `false` | Enables trusted proxy header processing. |
| `web/trusted_proxies` | empty | Trusted proxy addresses or CIDR ranges. |
| `web/public_https_host` | empty | Public HTTPS host used in HTTP-to-HTTPS redirects and in Local CLI password set/reset links. |
| `web/tls_certificate_file` | empty | PEM certificate chain file. |
| `web/tls_private_key_file` | empty | PEM private key file. |
| `web/tls_reload_interval_seconds` | `0` | TLS reload interval. `0` disables periodic reload. |
| `web/threads` | ideal thread count | Request worker thread count. |
| `web/storage_file` | app data location / `booking-resources-web.sqlite` | Web SQLite storage path. |
| `web/page_title` | `Booking Resources` | Base web page title. |
| `web/auth_methods` | `sign-without-registration` | Enabled web authorization methods. `sign-without-registration` enables the profile-and-cookie flow. `log-in-only` enables local login without public registration. `log-in-with-registration` enables local login and registration. `sign-without-registration` can be combined with one login method. If both login methods are set, `log-in-with-registration` is kept. Legacy aliases `web-profile` and `local-password` map to `sign-without-registration` and `log-in-with-registration`. |

Web configuration file locations:

| Source | Linux/Unix path | Windows path | Notes |
| --- | --- | --- | --- |
| `--config-file <file>` | exact file path | exact file path | Highest-priority explicit INI file. |
| `BOOKING_RESOURCES_BOT_WEB_CONFIG_FILE` | exact file path | exact file path | Used when `--config-file` is not set. |
| Qt `UserScope` | `$XDG_CONFIG_HOME/BookingResourcesBot-Web/web.ini`, or `~/.config/BookingResourcesBot-Web/web.ini` when `XDG_CONFIG_HOME` is not set | `%APPDATA%\BookingResourcesBot-Web\web.ini` | Default writable target when no explicit file is selected. |
| Qt `SystemScope` | first matching `$XDG_CONFIG_DIRS/BookingResourcesBot-Web/web.ini`, with `/etc/xdg/BookingResourcesBot-Web/web.ini` as the default fallback location | `%ProgramData%\BookingResourcesBot-Web\web.ini` | Read-only fallback via `QSettings::SystemScope`. |

Packaged Linux services use Qt `SystemScope` by default, so the installed `booking-resources-bot-web.service` reads `/etc/xdg/BookingResourcesBot-Web/web.ini` without passing `--config-file`. Windows packages ship WinSW service templates and matching system-scope config templates for `%ProgramData%\BookingResourcesBot-Web\web.ini`.

Environment variable:

| Variable | Values | Description |
| --- | --- | --- |
| `BOOKING_RESOURCES_BOT_WEB_CONFIG_FILE` | absolute or relative file path | Path to an explicit INI config file used when `--config-file` is not set. |

Complete `web.ini` example:

```ini
[web]
bind_address=127.0.0.1
http_port=8080
https_port=8443
http_enabled=true
shared_memory_key=booking-resources-web
shared_memory_secret=change-me-long-random-secret
https_enabled=false
external_https_enabled=false
allow_insecure_http=false
redirect_http_to_https=true
forwarded_headers_enabled=false
trusted_proxies=
public_https_host=
tls_certificate_file=
tls_private_key_file=
tls_reload_interval_seconds=0
threads=4
storage_file=/var/lib/booking-resources-bot/booking-resources-web.sqlite
page_title=Booking Resources
auth_methods=sign-without-registration, log-in-with-registration

[log]
file=/var/log/booking-resources-bot/web.log
```

`GET /health` is a public liveness endpoint for the web process itself. It returns a compact JSON response after HTTP/HTTPS listeners have started and does not require a live server IPC connection.

`log-in-only` and `log-in-with-registration` store credentials in the web SQLite storage only. Passwords are stored as salted PBKDF2-HMAC-SHA256 hashes; the stable user identity remains the generated `web:<uuid>` user key, not the login. The stored account `provider` value `local-password` is an account type, not an `auth_methods` value.

Plain application HTTP on a non-loopback address is refused unless `--allow-insecure-http` is set explicitly. HTTP redirect-only listeners are allowed on non-loopback addresses, so a direct deployment can listen on `80` and redirect to `443` while serving the application over HTTPS.

Direct public HTTPS example:

```sh
booking-resources-web \
  --bind-address 0.0.0.0 \
  --http-port 80 \
  --https-port 443 \
  --https \
  --redirect-http-to-https \
  --tls-certificate-file /etc/booking-resources-bot/fullchain.pem \
  --tls-private-key-file /etc/booking-resources-bot/privkey.pem \
  --tls-reload-interval 300 \
  --web-shared-memory-secret <secret>
```

For production, the recommended deployment is still to put `booking-resources-web` behind a reverse proxy such as nginx, Caddy, IIS, or Traefik. Run the web process on a loopback HTTP port, terminate TLS and redirect `80` to `443` at the proxy, and enable secure-cookie/HSTS behavior in the application:

```ini
[web]
bind_address=127.0.0.1
http_port=8080
https_enabled=false
external_https_enabled=true
forwarded_headers_enabled=true
trusted_proxies=127.0.0.1,
public_https_host=booking.example.com
```

When forwarded headers are enabled, `Forwarded`, `X-Forwarded-Proto`, `X-Forwarded-Host`, and `X-Forwarded-For` are accepted only from loopback or configured trusted proxies.
