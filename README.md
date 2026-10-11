# Booking Resources Bot

Booking Resources Bot is a Qt/C++17 project for managing shared resources through messenger bots, [Local CLI](docs/en/applications.md#booking-resources-cli), and a browser web chat. It is built with CMake and produces three executables:

- [`booking-resources-server`](docs/en/applications.md#booking-resources-server) - the main data process and messenger bot host.
- [`booking-resources-cli`](docs/en/applications.md#booking-resources-cli) - a local administrative shell that connects to a running server process.
- [`booking-resources-web`](docs/en/applications.md#booking-resources-web) - a standalone web interface that talks to the server through authenticated shared-memory IPC.

All backends use the same command and menu layer: Telegram, Kontur.Talk, Web, and [Local CLI](docs/en/applications.md#booking-resources-cli).

Runtime data is stored in SQLite, while application configuration is read from Qt INI settings and can be overridden with command-line options.

## Documentation

See the [documentation index](docs/docs.md).

## Quick Start

Start the server with at least one real backend configured. [Local CLI](docs/en/applications.md#booking-resources-cli) is always created by the server, but it is not enough by itself:

```sh
booking-resources-server --db-file resources_bot.sqlite --telegram-token <token> --admin telegram:<user_id>
```

To enable the web interface, start the server and web process with the same shared-memory key and secret. The web process can start before the server; it keeps serving HTTP and retries the IPC handshake until the server is available:

```sh
booking-resources-server --web-shared-memory-key booking-resources-web --web-shared-memory-secret <secret> --telegram-token <token>
booking-resources-web --web-shared-memory-key booking-resources-web --web-shared-memory-secret <secret>
```

For public web deployments, see the [`booking-resources-web`](docs/en/applications.md#booking-resources-web) production notes for HTTP/HTTPS ports, reverse-proxy headers, and TLS settings.

Use the [local CLI](docs/en/applications.md#booking-resources-cli) to discover and connect to a running server:

```sh
booking-resources-cli --list
booking-resources-cli --pid <server_pid>
booking-resources-cli --pid <server_pid> --command "/help"
```

## Development Notes

Reusable libraries live in `modules/`, while process-specific startup code lives under `server/`, `cli/`, and `web/`. Commands and menus are registered statically, so the server core is linked through the CMake helpers that preserve registration units in static archives. Built executables are written to `<build>/bin`.

Linux `.deb` packages for Ubuntu 24.04/26.04 with Qt 5 or Qt 6 can be built with Docker; see [Building Linux packages](docs/en/building.md).
