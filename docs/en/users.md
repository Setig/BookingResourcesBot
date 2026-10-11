# Users and Access Activation

## Table of Contents
- [Access Activation Flow](#access-activation-flow)
- [User Fields](#user-fields)
- [Roles](#roles)
- [Groups](#groups)
- [Multiple Keys for the Same Person](#multiple-keys-for-the-same-person)

Users are identified by a combined user key: a pair of messenger type and messenger-specific user id. The messenger type is the transport token, and the user id is the backend-specific id. The stable text form is:

```text
<messenger>:<user_id>
```

Examples:

```text
telegram:123456
kontur-talk:employee-id
web:3c2b4c5d-6e7f-48a9-9c10-111213141516
```

Supported messenger tokens are `telegram`, `kontur-talk`, `web`, and `local-cli`. The key must not contain spaces or parentheses. The `local-cli` identity is internal and is treated as already active/admin for local administration.

## Access Activation Flow

Access activation is administrator-controlled.

1. A new messenger or web user tries to interact with the bot.
2. If the user is not present in the server user list, the server denies the interaction, replies with the combined user key to share with an administrator, and records the user in the recent users awaiting access activation list.
3. An administrator opens [`/pending_access`](usage.md#cmd-pending-access) or the admin menu section "Users awaiting access activation".
4. The administrator adds the user with [`/add_user`](usage.md#cmd-add-user) `<messenger:id> ...` or through the menu.
5. The server verifies the user through the matching backend, creates the server user, clears the pending-access record, and sends a best-effort direct notification that mentions [`/start`](usage.md#cmd-start): "Access has been activated. You can now use the bot. Send /start to begin with commands."

For Web users, `/api/authorize` (`sign-without-registration`) or `/api/register` (`log-in-with-registration`) creates a web-local user id and session first. The web process then sends an `authorize_user` IPC request to the server, so the server records `web:<uuid>` in the recent users awaiting access activation list. The user can open the web chat, but server-backed actions remain limited until an administrator activates access for that `web:<uuid>` key. With a local login/password account, the login is only a web-local credential used to restore the same generated user id; it is not the server identity. The stored web `provider` value remains an account type (`authorization` or `local-password`) and is not an `auth_methods` value.

An administrator can also create a web-local login/password user through [Local CLI](applications.md#booking-resources-cli) `/add_user` without a working password when `log-in-only` or `log-in-with-registration` is enabled. That command writes only to the web SQLite store and does not call `authorize_user`. The printed `/set-password.html` link lets the person set the first password; the first successful `/api/login` then records the user on the server. `/reset_password` can attach a login to an existing user without a login or invalidate the current password and issue a `/reset-password.html` link for an account that already has a login. `/delete_user` removes only the web-local user; the server `web:<uuid>` account is left unchanged.

Users awaiting access activation are protected by a limiter: an attempt counter for denied interactions. Repeated attempts can lead to blacklist, which is the server block list for user keys:

- The first pending-access interactions receive the configured access-denied reply.
- Starting with the 3rd attempt, the reply includes a warning about remaining attempts.
- On the 10th attempt, the user is automatically blacklisted for 7 days.
- Recent users awaiting access activation are retained for the latest 100 entries and 3 days.

The reply text can be customized with [`/server_settings`](usage.md#cmd-server-settings) `set access.pending_access_user_reply <text>`; the server still appends the user's `Your ID: <messenger>:<user_id>` line.

## User Fields

Main fields:

| Field | Meaning |
| --- | --- |
| `messenger` | Messenger backend: `telegram`, `kontur-talk`, `web`, or `local-cli`. |
| `userId` | Messenger-specific user identifier. |
| `userKey` / `userKeyString` | Combined key, for example `telegram:1234`. |
| `firstName` | Display first name. Trimmed before storage. |
| `lastName` | Display last name. Trimmed before storage. |
| `locale` | Optional preferred locale. Empty means messenger/system/default fallback. In the web UI, that messenger language is the browser locale (`en` or `ru`). Built-in UI locales include `en`, `ru`, and `ru_stand`. |
| `timezone` | Optional fixed UTC offset for date/time rendering and parsing. Empty means server default. |
| `occupationNotifyMode` | Occupation notification mode: `all`, `mine`, or `off`. `mine` means only events involving the user. |
| `roleName` | Aggregate role: `user`, `admin`, or `fixed_admin`. |
| `groups` | Groups where the user has access. Empty/root is displayed as "Root (all groups)". |
| `groupRoles` | Per-group role entries in the form `group=user` or `group=admin`. |

Default values for a new non-admin user:

- Role: `user`.
- Group: all minimal administered group roots of the administrator who creates the account. For a server/root administrator this is the root group.
- Locale: default.
- Time zone: default.
- Occupation notifications: only events involving the user (`mine`).

## Roles

| Role | Description |
| --- | --- |
| `user` | Can use regular bot commands and menus for resources visible to their groups. |
| `admin` | Can manage users, resources, blacklist, queues, and notifications inside the group subtrees they administer. |
| `fixed_admin` | Server-level fixed administrator active during a server run that includes matching `--admin` or config `admin_ids`. Can manage server settings and all groups. |

`fixed_admin` cannot be assigned through normal user-management commands. It is reserved for server startup/configuration and is not stored as a persistent role. If the same account is no longer listed in `--admin` or `admin_ids` on a later run, it loads as a regular `admin`.

## Groups

Groups are normalized slash-separated paths:

```text
department
department/team
```

Backslashes are normalized to slashes, whitespace around path segments is trimmed, and an empty group means the root group. Root covers the complete tree and is displayed as "Root (all groups)". Resource group lists on [`/add_resource`](usage.md#cmd-add-resource), [`/update_resource`](usage.md#cmd-update-resource), and the resource wizard reject tokens that contain `=` or empty path segments (for example `office//floor1`).

Groups are hierarchical. Access to a parent group applies to nested groups. For example, access to `office` also covers `office/floor1`.

Users and resources both have groups. A user sees or manages resources when the user's group permissions contain the resource group.

When a scoped administrator omits groups while adding a user or resource, all minimal roots of
that administrator's `admin` subtrees are assigned. Explicit groups replace this default and may
name any descendant of those roots. Mutations of an existing multi-group object require
administrative access to every old and every new group, so an administrator cannot move or delete
an object through a sibling or parent group.

Pending-access entries do not have a group until activation. Only server/root administrators can
browse that global list; scoped administrators can still activate a known `messenger:id`, which is
assigned to their administered roots when groups are omitted. Global occupation-duration settings
are also restricted to server/root administrators.

Admin group roles are expressed as comma-separated entries:

```text
office=user,office/floor1=admin
```

## Multiple Keys for the Same Person

The system does not merge identities across messengers. If the same physical person has several combined user keys, each key is a separate server user:

- `telegram:1234` and `web:...` are different users.
- Names can be identical, but permissions, groups, settings, blacklist state, queues, schedules, and statistics are tied to the key.
- Activate access for every key that the person will use.
- Grant equivalent roles/groups manually when the person needs the same access in several interfaces.

This is important for Web usage: after web authorization the user receives a `web:<uuid>` id. Even if the same person already has Telegram access, access for the web id must be activated separately before the web chat can use server-backed commands. A local login/password can restore the same web id, but it does not merge it with any other messenger identity.
