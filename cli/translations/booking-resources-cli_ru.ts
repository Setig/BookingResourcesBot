<?xml version="1.0" encoding="utf-8"?>
<!DOCTYPE TS>
<TS version="2.1" language="ru_RU">
<context>
    <name>CliCommandLineParser</name>
    <message>
        <source>Local administrative shell for booking-resources-server and booking-resources-web.</source>
        <translation>Local CLI - административная оболочка для booking-resources-server и booking-resources-web.</translation>
    </message>
    <message>
        <source>List running booking-resources-server and booking-resources-web Local CLI processes.</source>
        <translation>Показать запущенные процессы Local CLI booking-resources-server и booking-resources-web.</translation>
    </message>
    <message>
        <source>PID of the booking-resources-server or booking-resources-web process to connect to.</source>
        <translation>PID процесса booking-resources-server или booking-resources-web, к которому нужно подключиться.</translation>
    </message>
    <message>
        <source>Shared-memory key of the booking-resources-web process to connect to.</source>
        <translation>Ключ общей памяти процесса booking-resources-web, к которому нужно подключиться.</translation>
    </message>
    <message>
        <source>Execute a single admin command and exit (requires --pid or --web-shared-memory-key).</source>
        <translation>Выполнить одну админскую команду и выйти (требуется --pid или --web-shared-memory-key).</translation>
    </message>
    <message>
        <source>PID to connect to (optional when --pid is set).</source>
        <translation>PID для подключения (можно опустить, если задан --pid).</translation>
    </message>
    <message>
        <source>Invalid PID specified via --pid option.</source>
        <translation>Указан недопустимый PID в параметре --pid.</translation>
    </message>
    <message>
        <source>Invalid PID specified as positional argument.</source>
        <translation>Указан недопустимый PID в позиционном аргументе.</translation>
    </message>
    <message>
        <source>--command requires a target process. Provide --pid &lt;pid&gt;, a positional PID, or --web-shared-memory-key &lt;key&gt;.</source>
        <translation>Для --command требуется целевой процесс. Укажите --pid &lt;pid&gt;, позиционный PID или --web-shared-memory-key &lt;key&gt;.</translation>
    </message>
    <message>
        <source>Command text must not be empty.</source>
        <translation>Текст команды не должен быть пустым.</translation>
    </message>
</context>
<context>
    <name>LocalCliClient</name>
    <message>
        <source>No active Local CLI processes were found.</source>
        <translation>Активные процессы Local CLI не найдены.</translation>
    </message>
    <message>
        <source>PID</source>
        <translation>PID</translation>
    </message>
    <message>
        <source>Kind</source>
        <translation>Kind</translation>
    </message>
    <message>
        <source>Status</source>
        <translation>Статус</translation>
    </message>
    <message>
        <source>Endpoint</source>
        <translation>Адрес</translation>
    </message>
    <message>
        <source>Key</source>
        <translation>Ключ</translation>
    </message>
    <message>
        <source>Started</source>
        <translation>Запуск</translation>
    </message>
    <message>
        <source>  (stale entry removed)</source>
        <translation>  (устаревшая запись удалена)</translation>
    </message>
    <message>
        <source>PID must be greater than zero.</source>
        <translation>PID должен быть больше нуля.</translation>
    </message>
    <message>
        <source>Web shared-memory key must not be empty.</source>
        <translation>Ключ общей памяти web не должен быть пустым.</translation>
    </message>
    <message>
        <source>No booking-resources-web Local CLI process was found for key &quot;%1&quot;.</source>
        <translation>Процесс Local CLI booking-resources-web с ключом &quot;%1&quot; не найден.</translation>
    </message>
    <message>
        <source>Failed to connect to web key &quot;%1&quot;: %2</source>
        <translation>Не удалось подключиться к web-ключу &quot;%1&quot;: %2</translation>
    </message>
    <message>
        <source>Failed to connect to PID %1: %2</source>
        <translation>Не удалось подключиться к PID %1: %2</translation>
    </message>
    <message>
        <source>Unknown meta-command. Type \help to see available shortcuts.</source>
        <translation>Неизвестная метакоманда. Введите \help, чтобы увидеть доступные сочетания.</translation>
    </message>
    <message>
        <source>Not connected to a Local CLI process. Use \connect &lt;pid&gt; or \connect web &lt;key&gt; first (see \help for details).</source>
        <translation>Нет подключения к процессу Local CLI. Сначала выполните \connect &lt;pid&gt; или \connect web &lt;key&gt; (подробнее см. \help).</translation>
    </message>
    <message>
        <source>Failed to send command to the bot.</source>
        <translation>Не удалось отправить команду боту.</translation>
    </message>
    <message>
        <source>Commands for the connected process must start with &apos;/&apos;. Type /help or \help.</source>
        <translation>Команды подключённого процесса должны начинаться с &apos;/&apos;. Введите /help или \help.</translation>
    </message>
    <message>
        <source>Unexpected handshake response from the server.</source>
        <translation>Неожиданный ответ рукопожатия от сервера.</translation>
    </message>
    <message>
        <source>Failed to send handshake to the server: %1</source>
        <translation>Не удалось отправить рукопожатие серверу: %1</translation>
    </message>
    <message>
        <source>Timed out while waiting for server handshake.</source>
        <translation>Истекло время ожидания рукопожатия сервера.</translation>
    </message>
    <message>
        <source>Failed to read server handshake: %1</source>
        <translation>Не удалось прочитать рукопожатие сервера: %1</translation>
    </message>
    <message>
        <source>[info] %1</source>
        <translation>[инфо] %1</translation>
    </message>
    <message>
        <source>Received unexpected frame type: %1</source>
        <translation>Получен неожиданный тип кадра: %1</translation>
    </message>
    <message>
        <source>Usage: \connect &lt;pid&gt; or \connect web &lt;key&gt;</source>
        <translation>Использование: \connect &lt;pid&gt; или \connect web &lt;key&gt;</translation>
    </message>
    <message>
        <source>Usage: \connect web &lt;key&gt;</source>
        <translation>Использование: \connect web &lt;key&gt;</translation>
    </message>
    <message>
        <source>Invalid PID value.</source>
        <translation>Недопустимое значение PID.</translation>
    </message>
    <message>
        <source>Connected to web key &quot;%1&quot;.</source>
        <translation>Подключено к web-ключу &quot;%1&quot;.</translation>
    </message>
    <message>
        <source>Connected to PID %1.</source>
        <translation>Подключено к PID %1.</translation>
    </message>
    <message>
        <source>Disconnected from the Local CLI process.</source>
        <translation>Соединение с процессом Local CLI разорвано.</translation>
    </message>
    <message>
        <source>Not connected.</source>
        <translation>Нет подключения.</translation>
    </message>
    <message>
        <source>Meta commands:
  \help                 Show Local CLI meta-command help
  \connect &lt;pid&gt;        Connect to a Local CLI process by PID
  \connect web &lt;key&gt;    Connect to booking-resources-web by shared-memory key
  \disconnect           Disconnect from the current process
  \list                 List running Local CLI processes
  \quit                 Disconnect and exit
Interactive input (TTY only):
  Up/Down               Previous and next commands
  Tab                   Complete meta-commands, slash-commands, and arguments
Command history is kept in memory for this process only.
Commands beginning with &apos;/&apos; are forwarded to the connected process.
Server connections accept admin slash-commands. Web connections accept web user-management commands.
Use /help on the connected process or \help for Local CLI meta commands.
</source>
        <translation>Метакоманды:
  \help                 Показать справку по метакомандам Local CLI
  \connect &lt;pid&gt;        Подключиться к процессу Local CLI по PID
  \connect web &lt;key&gt;    Подключиться к booking-resources-web по ключу общей памяти
  \disconnect           Отключиться от текущего процесса
  \list                 Показать запущенные процессы Local CLI
  \quit                 Отключиться и выйти
Интерактивный ввод (только TTY):
  Up/Down               Предыдущие и следующие команды
  Tab                   Дополнить метакоманды, слеш-команды и аргументы
История команд хранится в памяти только для этого процесса.
Команды, начинающиеся с &apos;/&apos;, пересылаются подключённому процессу.
Серверные подключения принимают админские слеш-команды. Web-подключения принимают команды управления web-пользователями.
Используйте /help на подключённом процессе или \help для метакоманд Local CLI.
</translation>
    </message>
    <message>
        <source>booking-resources-cli[web:%1]&gt; </source>
        <translation>booking-resources-cli[web:%1]&gt; </translation>
    </message>
    <message>
        <source>booking-resources-cli[%1]&gt; </source>
        <translation>booking-resources-cli[%1]&gt; </translation>
    </message>
    <message>
        <source>booking-resources-cli&gt; </source>
        <translation>booking-resources-cli&gt; </translation>
    </message>
    <message>
        <source>online</source>
        <translation>активен</translation>
    </message>
    <message>
        <source>stale</source>
        <translation>устарел</translation>
    </message>
    <message>
        <source>unknown</source>
        <translation>неизвестно</translation>
    </message>
    <message>
        <source>Incoming data exceeds the allowed buffer size.</source>
        <translation>Входящие данные превышают допустимый размер буфера.</translation>
    </message>
    <message>
        <source>Timed out while waiting for the server response.</source>
        <translation>Истекло время ожидания ответа сервера.</translation>
    </message>
    <message>
        <source>Failed to read the server response.</source>
        <translation>Не удалось прочитать ответ сервера.</translation>
    </message>
</context>
</TS>
