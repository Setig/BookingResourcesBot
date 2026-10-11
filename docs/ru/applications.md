# Приложения

## Оглавление
- [`booking-resources-server`](#booking-resources-server)
- [`booking-resources-cli`](#booking-resources-cli)
- [`booking-resources-web`](#booking-resources-web)

Booking Resources Bot состоит из трёх исполняемых приложений. Все они поддерживают стандартные опции `--help` и `--version` и используют Qt-переводы.

Бэкенд - транспорт, который подключает общий слой команд к Telegram, Kontur.Talk, Web или [Local CLI](#booking-resources-cli). Web IPC - аутентифицированный канал общей памяти между [`booking-resources-web`](#booking-resources-web) и [`booking-resources-server`](#booking-resources-server); оба процесса должны использовать одинаковые ключ и секрет общей памяти.

Общие переменные окружения:

| Переменная | Значения | Описание |
| --- | --- | --- |
| `BOOKING_BOT_LOCALE` | код локали, например `en`, `ru`, `ru_stand`, `ru_RU` или `ru-stand` | Используется в стандартном выборе локали, когда процесс инициализирует `BookingResourcesTranslator` без явно переданной предпочтительной локали. Это влияет на старт [`booking-resources-server`](#booking-resources-server) и [Local CLI](#booking-resources-cli); [`booking-resources-web`](#booking-resources-web) держит базовый переводчик на английском и использует локаль уровня запроса для словарей и локализованных HTTP-ответов. Приветствие и ответы команд web [Local CLI](#booking-resources-cli) используют тот же стандартный выбор локали (`BOOKING_BOT_LOCALE`, затем системная локаль). Поиск перевода идёт от точной/нормализованной локали к полной локали Qt, затем к языку без региона и в конце к встроенным английским строкам. |
| `QT_LOGGING_RULES` | стандартные правила категорий логирования Qt | Добавляет пользовательские фильтры Qt к общему набору правил по умолчанию, который начинается с `*.debug=false`. |

Пакетные поставки делятся на компоненты `booking-resources-bot-common`, `booking-resources-bot-server`, `booking-resources-bot-web` и `booking-resources-bot-cli`. Linux-пакеты устанавливают `booking-resources-bot-server.service` и `booking-resources-bot-web.service`, а Windows-пакеты включают шаблоны WinSW-сервисов для `BookingResourcesBotServer` и `BookingResourcesBotWeb`. Linux-пакеты `.deb` для Ubuntu 24.04/26.04 и Qt 5 или Qt 6 собираются через Docker; см. [сборку Linux-пакетов](building.md).

## `booking-resources-server`

Сервер - основной процесс. Он открывает SQLite-базу, инициализирует настройки во время работы, запускает бэкенды мессенджеров, хранит пользователей, ресурсы, очередь, расписание и статистику, а также поднимает точку подключения для [Local CLI](#booking-resources-cli). Для старта нужен хотя бы один реальный бэкенд: Telegram, Kontur.Talk или Web IPC; одного [Local CLI](#booking-resources-cli) недостаточно. Пока к процессу подключён клиент Local CLI, `Ctrl+C` и события выхода Qt не останавливают сервер. Закрытие окна консоли, выход из системы и выключение по-прежнему завершают процесс. В Linux `SIGINT` следует тому же правилу, что и `Ctrl+C`; `SIGTERM` и `SIGHUP` по-прежнему останавливают процесс.

Аргументы запуска:

| Опция | Значение | Описание |
| --- | --- | --- |
| `--config-file` | `file` | Использовать явный INI-файл конфигурации. Он переопределяет оба Qt scope-файла для чтения и становится основным файлом записи для обновления настроек. |
| `-l`, `--log-file` | `file` | Базовый путь для файлов логов. Без него лог пишется в консоль. |
| `--db-file` | `file` | SQLite-файл базы данных. Переопределяет `database/file`. |
| `--web-shared-memory-key` | `key` | Включает Web IPC и задаёт имя точки подключения общей памяти. |
| `--web-shared-memory-secret` | `secret` | Общий секрет для аутентификации кадров Web IPC. Обязателен при включённом Web IPC. |
| `--telegram-token` | `token` | Токен Telegram Bot API. Переопределяет `bots/telegram/token`. |
| `--kontur-talk-token` | `token` | JWT-токен бота Kontur.Talk. Переопределяет `bots/kontur-talk/token`. |
| `--admin` | `messenger:user_id` | Создаёт или обновляет фиксированного администратора на текущий запуск сервера. Можно передавать несколько раз. |

Конфигурация читается из многоуровневых Qt INI-настроек. Сервер использует `BookingResourcesBot-Server` и `config` как имена организации/приложения. Порядок разрешения такой: специализированные CLI-опции вроде `--db-file` или `--telegram-token` -> `--config-file` -> `BOOKING_RESOURCES_BOT_SERVER_CONFIG_FILE` -> Qt `UserScope` -> Qt `SystemScope` -> встроенные значения по умолчанию. Когда используется `--config-file` или `BOOKING_RESOURCES_BOT_SERVER_CONFIG_FILE`, этот явный файл становится основным файлом записи; иначе запись идёт в Qt `UserScope`, а `SystemScope` остаётся только резервным источником для чтения. Поддерживаемые ключи:

| Ключ | Описание |
| --- | --- |
| `database/file` | SQLite-файл базы. Если пусто, используется путь данных приложения и `resources_bot.sqlite`; запасной вариант - текущая директория. |
| `log/file` | Базовый путь для файлов логов. |
| `web/shared_memory_key` | Ключ общей памяти для Web IPC. |
| `web/shared_memory_secret` | Секрет Web IPC. |
| `bots/telegram/token` | Токен Telegram. Также читается устаревший ключ `telegram/token`. |
| `bots/kontur-talk/token` | Токен Kontur.Talk. |
| `bots/<messenger>/admin_ids` | Список идентификаторов фиксированных администраторов для мессенджера на текущий запуск сервера. Также читается устаревший ключ `telegram/admin_ids`. |

Пути к конфигурации сервера:

| Источник | Linux/Unix | Windows | Примечание |
| --- | --- | --- | --- |
| `--config-file <file>` | точный путь к файлу | точный путь к файлу | Явный INI-файл с наивысшим приоритетом. |
| `BOOKING_RESOURCES_BOT_SERVER_CONFIG_FILE` | точный путь к файлу | точный путь к файлу | Используется, если `--config-file` не задан. |
| Qt `UserScope` | `$XDG_CONFIG_HOME/BookingResourcesBot-Server/config.ini`, или `~/.config/BookingResourcesBot-Server/config.ini`, если `XDG_CONFIG_HOME` не задан | `%APPDATA%\BookingResourcesBot-Server\config.ini` | Стандартный файл записи, если явный файл не выбран. |
| Qt `SystemScope` | первое совпадение в `$XDG_CONFIG_DIRS/BookingResourcesBot-Server/config.ini`, по умолчанию `/etc/xdg/BookingResourcesBot-Server/config.ini` | `%ProgramData%\BookingResourcesBot-Server\config.ini` | Read-only fallback через `QSettings::SystemScope`. |

Если явный конфиг создаётся вручную до первого запуска, нужно заранее создать родительский каталог. Пакетный Linux-сервис по умолчанию использует Qt `SystemScope`, поэтому установленный `booking-resources-bot-server.service` читает `/etc/xdg/BookingResourcesBot-Server/config.ini` без передачи `--config-file`.

Переменная окружения:

| Переменная | Значения | Описание |
| --- | --- | --- |
| `BOOKING_RESOURCES_BOT_SERVER_CONFIG_FILE` | абсолютный или относительный путь к файлу | Путь к явному INI-файлу конфигурации, используемому, когда `--config-file` не задан. |

Полный пример `config.ini`:

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

`admin_ids` использует синтаксис списков Qt INI; если в написанном вручную списке только один идентификатор, оставьте значение списком с завершающей запятой, например `admin_ids=123456789,`. Поддерживаемые имена мессенджеров: `telegram`, `kontur-talk`, `web` и `local-cli`; идентификаторы `local-cli` равны PID клиентского процесса, поэтому обычно их не хранят в постоянной конфигурации.

Настройки сервера во время работы хранятся в SQLite и меняются серверным администратором через [`/server_settings`](usage.md#cmd-server-settings) (`list`, `get`, `set`). Числовые настройки принимают неотрицательные целые; `0`, `unlimited` и `disabled` означают отсутствие лимита или отключение там, где это применимо.

| Ключ | По умолчанию | Описание |
| --- | --- | --- |
| `queue.max_resources_per_user` | `0` | Максимум разных ресурсов, которые один пользователь может одновременно занимать или держать в расписании. `0` - без ограничения. |
| `queue.max_occupation_duration_minutes` | `0` | Максимальная длительность занятости в минутах. `0` - без ограничения. |
| `menu.update_mode` | `active` | `active` автообновляет открытые меню; `passive` показывает кнопки обновления. |
| `menu.session_timeout_minutes` | `30` | Таймаут неактивности сессии меню. `0` отключает таймаут. |
| `stats.enabled` | `disabled` | Включает сбор статистики. Принимает `enabled`/`disabled`, `on`/`off`, `true`/`false` или `1`/`0`. Когда статистика выключена, [`/statistics`](usage.md#cmd-statistics) и [`/help`](usage.md#cmd-help) `/statistics` отвечают, что она недоступна, а не считают команду неизвестной. |
| `stats.admins_only` | `admins_only` | `admins_only` ограничивает [`/statistics`](usage.md#cmd-statistics) админами; `all_users` разрешает пользователям с активным доступом. Не-админ при вводе команды или [`/help`](usage.md#cmd-help) `/statistics` всё равно получает отказ в доступе. |
| `ui.date_time_format` | `auto` | Формат отображения и ввода даты/времени. `auto`/`default` использует локализованный встроенный формат. |
| `access.pending_access_user_reply` | `default` | Ответ пользователям, ожидающим активации доступа. `auto`/`default` возвращает встроенный локализованный текст. |

Стандартные длительности занятости: `0`, `5`, `30`, `60`, `120` и `240` минут. `0` означает бессрочно и не удаляется.

Сервер включает WAL-режим SQLite, выполняет `PRAGMA quick_check` при старте, создаёт резервную копию через `VACUUM INTO <db>.bak`, а повреждённые базы переименовывает в `*.corrupted.<timestamp>`.

## `booking-resources-cli`

[Local CLI](#booking-resources-cli) - локальная админская оболочка для запущенного [`booking-resources-server`](#booking-resources-server) или [`booking-resources-web`](#booking-resources-web). Она подключается к локальной IPC-точке этого процесса. Подключение к серверу пересылает слеш-команды как админские сообщения `Local CLI`. Подключение к web отправляет команды управления web-пользователями в web-процесс. У CLI нет `--web-shared-memory-secret`: канал Local CLI считается доверенным локальным сокетом, как Local CLI сервера.

Аргументы запуска:

| Опция | Значение | Описание |
| --- | --- | --- |
| `-l`, `--list` | нет | Показать запущенные процессы server и web Local CLI, включая kind и ключ общей памяти web, и удалить устаревшие записи реестра. |
| `-p`, `--pid` | `pid` | PID процесса сервера или web для подключения. |
| позиционный `pid` | `pid` | Альтернативный способ указать PID процесса. |
| `--web-shared-memory-key` | `key` | Ключ общей памяти процесса [`booking-resources-web`](#booking-resources-web). Выбирает этот web Local CLI. |
| `-c`, `--command` | `command` | Выполнить одну команду и выйти. Требует `--pid`, позиционный PID или `--web-shared-memory-key`. |

Интерактивные служебные команды:

| Команда | Описание |
| --- | --- |
| `\help` | Показать справку CLI. |
| `\connect <pid>` | Подключиться к процессу Local CLI по PID. |
| `\connect web <key>` | Подключиться к [`booking-resources-web`](#booking-resources-web) по ключу общей памяти. |
| `\disconnect` | Отключиться от текущего процесса. |
| `\list` | Показать запущенные процессы Local CLI. |
| `\quit`, `\exit`, `\q` | Отключиться и выйти. |

В настоящей TTY интерактивная оболочка поддерживает Up/Down для предыдущих команд и Tab для дополнения метакоманд, слеш-команд и аргументов. История команд держится в памяти текущего процесса и забывается при выходе из CLI. `--command` и ввод из pipe остаются построчными, без истории и Tab.

Для web-подключения приглашение выглядит как `booking-resources-cli[web:booking-resources-web]>`.

Переменная окружения:

| Переменная | Значения | Описание |
| --- | --- | --- |
| `BOOKING_RESOURCES_CLI_COLOR` | `auto`, `always`, `on`, `1`, `true`, `never`, `off`, `0`, `false` | Управляет подсветкой приглашения CLI. По умолчанию `auto`. |

## `booking-resources-web`

Web-процесс отдаёт встроенный HTML/CSS/JS интерфейс и общается с сервером через аутентифицированный IPC общей памяти. У него своя SQLite-база для web-пользователей, сессий, событий, файлов и кэша команд. Он также поднимает точку [Local CLI](#booking-resources-cli). [`booking-resources-cli`](#booking-resources-cli) находит её по `--web-shared-memory-key` (тот же ключ, что у Web IPC) или по PID. Рукопожатие Local CLI - `hello` / `hello-ack`, без `web-shared-memory-secret`. Пока к процессу подключён клиент Local CLI, `Ctrl+C` и события выхода Qt не останавливают web. Закрытие окна консоли, выход из системы и выключение по-прежнему завершают процесс. В Linux `SIGINT` следует тому же правилу, что и `Ctrl+C`; `SIGTERM` и `SIGHUP` по-прежнему останавливают процесс.

HTTP- и HTTPS-слушатели поднимаются, даже если [`booking-resources-server`](#booking-resources-server) ещё не запущен. Web-процесс в фоне делает attach к сегментам общей памяти и проверяет рукопожатие `ping`/`pong`, затем повторяет попытки каждые несколько секунд, пока сервер не станет доступен, и снова после потери IPC. Пока IPC недоступен, API-маршруты, которым нужен сервер, отвечают `503` с `error_code` `server_unavailable`. `GET /api/events` остаётся доступен и включает `server_available`, чтобы чат мог показать `Сервер недоступен.` Браузер показывает `Web-сервер недоступен.` только если не может достучаться до web-процесса. Отсутствующий `web/shared_memory_secret` по-прежнему ошибка запуска.

Аргументы запуска:

| Опция | Значение | Описание |
| --- | --- | --- |
| `--config-file` | `file` | Использовать явный INI-файл конфигурации. Он переопределяет оба Qt scope-файла для чтения и становится основным файлом записи для обновления настроек. |
| `--bind-address` | `address` | Адрес привязки HTTP и HTTPS. |
| `--http-port` | `port` | HTTP-порт. По умолчанию `8080`. |
| `--https-port` | `port` | HTTPS-порт. По умолчанию `8443`, когда HTTPS включён. Для прямого публичного HTTPS используйте `443`. |
| `--no-http` | нет | Отключить HTTP listener. |
| `--redirect-http-to-https` | нет | Перенаправлять HTTP-запросы на HTTPS, когда HTTPS включён. |
| `--web-shared-memory-key` | `key` | Ключ общей памяти для подключения к серверу. |
| `--web-shared-memory-secret` | `secret` | Общий секрет для IPC. Обязателен. |
| `-l`, `--log-file` | `file` | Базовый путь для файлов логов. |
| `--https` | нет | Включить HTTPS. |
| `--no-https` | нет | Отключить HTTPS даже при включении в настройках. |
| `--external-https` | нет | Считать запросы пришедшими через внешний HTTPS, например обратный прокси. |
| `--allow-insecure-http` | нет | Разрешить обычный HTTP на адресах не из loopback-диапазона. |
| `--forwarded-headers` | нет | Доверять `Forwarded` и `X-Forwarded-*` заголовкам от loopback или настроенных доверенных прокси. |
| `--trusted-proxy` | `address` | Доверенный адрес прокси или CIDR. Можно повторять. Loopback-прокси доверяются при включённых forwarded headers. |
| `--public-https-host` | `host` | Публичный HTTPS host для HTTP-to-HTTPS redirect и для ссылок установки/сброса пароля в Local CLI. |
| `--tls-certificate-file` | `file` | PEM-файл цепочки сертификатов. |
| `--tls-private-key-file` | `file` | PEM-файл приватного ключа. |
| `--tls-reload-interval` | `seconds` | Периодически перезагружать TLS-сертификат и приватный ключ для новых соединений. `0` отключает перезагрузку. |
| `--threads` | `count` | Максимум рабочих потоков для запросов. Минимум `1`. |
| `--storage-file` | `file` | SQLite-файл web-хранилища. |
| `--web-page-title` | `title` | Базовый заголовок web-страниц. |
| `--web-auth-method` | `method` | Включённый способ web-авторизации. Поддерживаются `sign-without-registration`, `log-in-only` и `log-in-with-registration`. Можно повторять или задавать через запятую. По умолчанию `sign-without-registration`. Старые имена `web-profile` и `local-password` принимаются как алиасы. |

Конфигурация читается из многоуровневых Qt INI-настроек. Web-процесс использует `BookingResourcesBot-Web` и `web` как имена организации/приложения. Порядок разрешения такой: специализированные CLI-опции вроде `--storage-file` или `--https` -> `--config-file` -> `BOOKING_RESOURCES_BOT_WEB_CONFIG_FILE` -> Qt `UserScope` -> Qt `SystemScope` -> встроенные значения по умолчанию. Когда используется `--config-file` или `BOOKING_RESOURCES_BOT_WEB_CONFIG_FILE`, этот явный файл становится основным файлом записи; иначе запись идёт в Qt `UserScope`, а `SystemScope` остаётся только резервным источником для чтения. Поддерживаемые ключи:

| Ключ | По умолчанию | Описание |
| --- | --- | --- |
| `web/bind_address` | `127.0.0.1` | Адрес привязки. |
| `web/http_port` | `8080` | HTTP-порт. |
| `web/https_port` | `8443` | HTTPS-порт, когда HTTPS включён. |
| `web/http_enabled` | `true` | Включает HTTP listener. |
| `web/redirect_http_to_https` | `true` | Перенаправляет HTTP-запросы на HTTPS, когда HTTPS включён. |
| `web/shared_memory_key` | `booking-resources-web` | IPC-ключ. Должен совпадать с сервером. |
| `web/shared_memory_secret` | пусто | IPC-секрет. Должен совпадать с сервером. |
| `log/file` | пусто | Базовый путь логов. |
| `web/https_enabled` | `false` | Включает HTTPS при заданных сертификате и ключе. |
| `web/external_https_enabled` | `false` | Включает поведение защищённых cookie и HSTS для внешнего HTTPS. |
| `web/allow_insecure_http` | `false` | Разрешает обычный HTTP на адресах не из loopback-диапазона. |
| `web/forwarded_headers_enabled` | `false` | Включает обработку заголовков доверенного прокси. |
| `web/trusted_proxies` | пусто | Доверенные адреса прокси или CIDR-диапазоны. |
| `web/public_https_host` | пусто | Публичный HTTPS host для HTTP-to-HTTPS redirect и для ссылок установки/сброса пароля в Local CLI. |
| `web/tls_certificate_file` | пусто | PEM-файл цепочки сертификатов. |
| `web/tls_private_key_file` | пусто | PEM-файл приватного ключа. |
| `web/tls_reload_interval_seconds` | `0` | Интервал перезагрузки TLS. `0` отключает периодическую перезагрузку. |
| `web/threads` | оптимальное количество потоков | Количество рабочих потоков. |
| `web/storage_file` | каталог данных приложения / `booking-resources-web.sqlite` | Путь web SQLite-хранилища. |
| `web/page_title` | `Booking Resources` | Базовый заголовок web-страниц. |
| `web/auth_methods` | `sign-without-registration` | Включённые способы web-авторизации. `sign-without-registration` включает сценарий профиля и cookie. `log-in-only` включает локальный вход без публичной регистрации. `log-in-with-registration` включает локальный вход и регистрацию. `sign-without-registration` можно сочетать с одним login-способом. Если заданы оба login-способа, остаётся `log-in-with-registration`. Старые имена `web-profile` и `local-password` соответствуют `sign-without-registration` и `log-in-with-registration`. |

Пути к конфигурации web-приложения:

| Источник | Linux/Unix | Windows | Примечание |
| --- | --- | --- | --- |
| `--config-file <file>` | точный путь к файлу | точный путь к файлу | Явный INI-файл с наивысшим приоритетом. |
| `BOOKING_RESOURCES_BOT_WEB_CONFIG_FILE` | точный путь к файлу | точный путь к файлу | Используется, если `--config-file` не задан. |
| Qt `UserScope` | `$XDG_CONFIG_HOME/BookingResourcesBot-Web/web.ini`, или `~/.config/BookingResourcesBot-Web/web.ini`, если `XDG_CONFIG_HOME` не задан | `%APPDATA%\BookingResourcesBot-Web\web.ini` | Стандартный файл записи, если явный файл не выбран. |
| Qt `SystemScope` | первое совпадение в `$XDG_CONFIG_DIRS/BookingResourcesBot-Web/web.ini`, по умолчанию `/etc/xdg/BookingResourcesBot-Web/web.ini` | `%ProgramData%\BookingResourcesBot-Web\web.ini` | Read-only fallback через `QSettings::SystemScope`. |

Пакетный Linux-сервис по умолчанию использует Qt `SystemScope`, поэтому установленный `booking-resources-bot-web.service` читает `/etc/xdg/BookingResourcesBot-Web/web.ini` без передачи `--config-file`. Windows-пакеты включают шаблоны WinSW-сервисов и соответствующие шаблоны system-scope конфигурации для `%ProgramData%\BookingResourcesBot-Web\web.ini`.

Переменная окружения:

| Переменная | Значения | Описание |
| --- | --- | --- |
| `BOOKING_RESOURCES_BOT_WEB_CONFIG_FILE` | абсолютный или относительный путь к файлу | Путь к явному INI-файлу конфигурации, используемому, когда `--config-file` не задан. |

Полный пример `web.ini`:

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

`GET /health` — публичный liveness endpoint самого web-процесса. Он возвращает компактный JSON-ответ после запуска HTTP/HTTPS-слушателей и не требует живого IPC-соединения с сервером.

`log-in-only` и `log-in-with-registration` хранят учётные данные только в web SQLite-хранилище. Пароли сохраняются как salted PBKDF2-HMAC-SHA256 hash; стабильной идентичностью пользователя остаётся сгенерированный ключ `web:<uuid>`, а не логин. Значение `provider` в хранилище `local-password` - это тип учётки, а не значение `auth_methods`.

Обычный HTTP для приложения на адресе не из loopback-диапазона запрещён, если явно не передан `--allow-insecure-http`. HTTP listener, который только перенаправляет на HTTPS, разрешён на внешнем адресе, поэтому прямой запуск может слушать `80` и перенаправлять на `443`, а приложение обслуживать через HTTPS.

Пример прямого публичного HTTPS:

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

Для production рекомендуется запускать `booking-resources-web` за reverse proxy, например nginx, Caddy, IIS или Traefik. Web-процесс слушает loopback HTTP-порт, proxy завершает TLS и перенаправляет `80` на `443`, а приложение включает поведение защищённых cookie и HSTS:

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

Когда forwarded headers включены, `Forwarded`, `X-Forwarded-Proto`, `X-Forwarded-Host` и `X-Forwarded-For` принимаются только от loopback или настроенных доверенных прокси.
