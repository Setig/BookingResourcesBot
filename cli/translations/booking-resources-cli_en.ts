<?xml version="1.0" encoding="utf-8"?>
<!DOCTYPE TS>
<TS version="2.1" language="en_US">
<context>
    <name>CliCommandLineParser</name>
    <message>
        <source>Local administrative shell for booking-resources-server and booking-resources-web.</source>
        <translation>Local administrative shell for booking-resources-server and booking-resources-web.</translation>
    </message>
    <message>
        <source>List running booking-resources-server and booking-resources-web Local CLI processes.</source>
        <translation>List running booking-resources-server and booking-resources-web Local CLI processes.</translation>
    </message>
    <message>
        <source>PID of the booking-resources-server or booking-resources-web process to connect to.</source>
        <translation>PID of the booking-resources-server or booking-resources-web process to connect to.</translation>
    </message>
    <message>
        <source>Shared-memory key of the booking-resources-web process to connect to.</source>
        <translation>Shared-memory key of the booking-resources-web process to connect to.</translation>
    </message>
    <message>
        <source>Execute a single admin command and exit (requires --pid or --web-shared-memory-key).</source>
        <translation>Execute a single admin command and exit (requires --pid or --web-shared-memory-key).</translation>
    </message>
    <message>
        <source>PID to connect to (optional when --pid is set).</source>
        <translation>PID to connect to (optional when --pid is set).</translation>
    </message>
    <message>
        <source>Invalid PID specified via --pid option.</source>
        <translation>Invalid PID specified via --pid option.</translation>
    </message>
    <message>
        <source>Invalid PID specified as positional argument.</source>
        <translation>Invalid PID specified as positional argument.</translation>
    </message>
    <message>
        <source>--command requires a target process. Provide --pid &lt;pid&gt;, a positional PID, or --web-shared-memory-key &lt;key&gt;.</source>
        <translation>--command requires a target process. Provide --pid &lt;pid&gt;, a positional PID, or --web-shared-memory-key &lt;key&gt;.</translation>
    </message>
    <message>
        <source>Command text must not be empty.</source>
        <translation>Command text must not be empty.</translation>
    </message>
</context>
<context>
    <name>LocalCliClient</name>
    <message>
        <source>No active Local CLI processes were found.</source>
        <translation>No active Local CLI processes were found.</translation>
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
        <translation>Status</translation>
    </message>
    <message>
        <source>Endpoint</source>
        <translation>Endpoint</translation>
    </message>
    <message>
        <source>Key</source>
        <translation>Key</translation>
    </message>
    <message>
        <source>Started</source>
        <translation>Started</translation>
    </message>
    <message>
        <source>  (stale entry removed)</source>
        <translation>  (stale entry removed)</translation>
    </message>
    <message>
        <source>PID must be greater than zero.</source>
        <translation>PID must be greater than zero.</translation>
    </message>
    <message>
        <source>Web shared-memory key must not be empty.</source>
        <translation>Web shared-memory key must not be empty.</translation>
    </message>
    <message>
        <source>No booking-resources-web Local CLI process was found for key &quot;%1&quot;.</source>
        <translation>No booking-resources-web Local CLI process was found for key &quot;%1&quot;.</translation>
    </message>
    <message>
        <source>Failed to connect to web key &quot;%1&quot;: %2</source>
        <translation>Failed to connect to web key &quot;%1&quot;: %2</translation>
    </message>
    <message>
        <source>Failed to connect to PID %1: %2</source>
        <translation>Failed to connect to PID %1: %2</translation>
    </message>
    <message>
        <source>Unknown meta-command. Type \help to see available shortcuts.</source>
        <translation>Unknown meta-command. Type \help to see available shortcuts.</translation>
    </message>
    <message>
        <source>Not connected to a Local CLI process. Use \connect &lt;pid&gt; or \connect web &lt;key&gt; first (see \help for details).</source>
        <translation>Not connected to a Local CLI process. Use \connect &lt;pid&gt; or \connect web &lt;key&gt; first (see \help for details).</translation>
    </message>
    <message>
        <source>Failed to send command to the bot.</source>
        <translation>Failed to send command to the bot.</translation>
    </message>
    <message>
        <source>Commands for the connected process must start with &apos;/&apos;. Type /help or \help.</source>
        <translation>Commands for the connected process must start with &apos;/&apos;. Type /help or \help.</translation>
    </message>
    <message>
        <source>Unexpected handshake response from the server.</source>
        <translation>Unexpected handshake response from the server.</translation>
    </message>
    <message>
        <source>Failed to send handshake to the server: %1</source>
        <translation>Failed to send handshake to the server: %1</translation>
    </message>
    <message>
        <source>Timed out while waiting for server handshake.</source>
        <translation>Timed out while waiting for server handshake.</translation>
    </message>
    <message>
        <source>Failed to read server handshake: %1</source>
        <translation>Failed to read server handshake: %1</translation>
    </message>
    <message>
        <source>[info] %1</source>
        <translation>[info] %1</translation>
    </message>
    <message>
        <source>Received unexpected frame type: %1</source>
        <translation>Received unexpected frame type: %1</translation>
    </message>
    <message>
        <source>Usage: \connect &lt;pid&gt; or \connect web &lt;key&gt;</source>
        <translation>Usage: \connect &lt;pid&gt; or \connect web &lt;key&gt;</translation>
    </message>
    <message>
        <source>Usage: \connect web &lt;key&gt;</source>
        <translation>Usage: \connect web &lt;key&gt;</translation>
    </message>
    <message>
        <source>Invalid PID value.</source>
        <translation>Invalid PID value.</translation>
    </message>
    <message>
        <source>Connected to web key &quot;%1&quot;.</source>
        <translation>Connected to web key &quot;%1&quot;.</translation>
    </message>
    <message>
        <source>Connected to PID %1.</source>
        <translation>Connected to PID %1.</translation>
    </message>
    <message>
        <source>Disconnected from the Local CLI process.</source>
        <translation>Disconnected from the Local CLI process.</translation>
    </message>
    <message>
        <source>Not connected.</source>
        <translation>Not connected.</translation>
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
        <translation>Meta commands:
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
        <translation>online</translation>
    </message>
    <message>
        <source>stale</source>
        <translation>stale</translation>
    </message>
    <message>
        <source>unknown</source>
        <translation>unknown</translation>
    </message>
    <message>
        <source>Incoming data exceeds the allowed buffer size.</source>
        <translation>Incoming data exceeds the allowed buffer size.</translation>
    </message>
    <message>
        <source>Timed out while waiting for the server response.</source>
        <translation>Timed out while waiting for the server response.</translation>
    </message>
    <message>
        <source>Failed to read the server response.</source>
        <translation>Failed to read the server response.</translation>
    </message>
</context>
</TS>
