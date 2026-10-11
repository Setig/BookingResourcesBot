# Resource Queue and Scheduling

## Table of Contents
- [What the Queue Stores](#what-the-queue-stores)
- [User Commands](#user-commands)
- [Menu Workflow](#menu-workflow)
- [Administrator Capabilities](#administrator-capabilities)
- [Limits and Automatic Events](#limits-and-automatic-events)

A resource queue is the list of entries for one resource: the current occupation, users waiting for release, and future scheduled reservations. Fixed time windows for the same resource cannot overlap. When a resource is occupied, [`/occupy`](usage.md#cmd-occupy) places the user into a FIFO wait list that starts `when the resource is released`.

## What the Queue Stores

Current occupation is created with [`/occupy`](usage.md#cmd-occupy) or the `Occupy` button in `Resource overview`. The entry starts now and ends after the selected duration; `0` means indefinite when policy allows it.

If the resource is already occupied and has no fixed future reservations, [`/occupy`](usage.md#cmd-occupy) creates an after-release queue entry instead. Its displayed start is `when the resource is released`; the requested duration can also be `0` for an indefinite turn. The start message is sent only when that queued entry actually becomes the current occupation.

A future reservation is created with [`/schedule`](usage.md#cmd-schedule) or the `Plan usage` button in `Resource overview`. A reservation has a start time and an optional end time. If the end is omitted, the reservation is indefinite.

Fixed future reservations and after-release queue entries are not mixed for the same resource. A fixed schedule is rejected while users are waiting for release, and waiting for release is rejected while the resource already has fixed future reservations.

Queue status can be viewed with [`/queue`](usage.md#cmd-queue), with the `View queue` button in `Resource overview`, or directly from the `Resource queue` menu. The response shows the current occupant, the schedule, and local dates/times using the viewer's settings.

## User Commands

Basic slash-command flow:

```text
/occupy Printer 5 min
/occupy Printer 5 minutes
/occupy Printer 2 hours
/queue Printer
/release Printer
/release_all
```

If [`/occupy`](usage.md#cmd-occupy) omits duration, the bot shows the allowed durations and their option numbers:

```text
/occupy Printer
```

Schedule a future window:

```text
/schedule Printer "2026-02-03 10:00" "2026-02-03 12:00"
/schedule Printer "2026-02-03 18:00"
```

Date/time input is interpreted in the user's time zone. The format comes from `ui.date_time_format`; ISO input is accepted too. Use quotes when a date/time value contains spaces.

Wait until the resource is released:

```text
/schedule Printer 0 5 min
/schedule Printer 0 5 minutes
/schedule Printer 0 2 hours
/schedule Printer 0 0
```

For this form, start value `0` means `when the resource is released`; the optional third argument is a duration or a duration option number. Omitted duration means `0`. The readable start value `"when the resource is released"` is also accepted as an alternative.

Remove your reservations for a resource:

```text
/unschedule Printer
```

[`/soonest`](usage.md#cmd-soonest) shows resources that will release first:

```text
/soonest
/soonest 10
```

Default count is `5`, capped at `20`.

## Menu Workflow

Open [`/menu`](usage.md#cmd-menu), then go to `Booking Resources` and choose `Resource cards`, then a specific resource, or the quick lists `Free resources` / `Releases soon`.

In `Resource overview`, the main actions are:

- `Occupy` button in `Resource overview` - choose an occupation duration with buttons. If the resource is occupied, this queues the user for `when the resource is released`. After a successful occupy, the current menu message becomes the same result text as [`/occupy`](usage.md#cmd-occupy), including start instructions when the occupation starts immediately, and a new `Resource overview` menu is sent.
- `Release` button in `Resource overview` - release the resource when you are the current occupant.
- `Plan usage` button in `Resource overview` - open `Scheduling`, then choose start and end date/time.
- `Cancel schedule` in `Resource overview` and `Cancel my slot` in `Scheduling` - remove your reservation for the resource when one exists.
- `View queue` button in `Resource overview` - open `Resource queue` with current occupation and schedule details.

In `Scheduling`, choose a date from the next days and then a time in 30-minute steps. The end step includes the `Indefinite` button in `Scheduling`. Before saving, the menu shows a review page and the `Accept` button in `Scheduling`.

In `Resource queue`, a regular user can press the `Leave queue` button when they have an entry for the resource, including an after-release waiting entry. In `passive` update mode, `Resource queue` also shows the `Refresh list` and `Refresh queue` buttons.

## Administrator Capabilities

Administrators can manage queue entries only when they administer every group of both the resource
and the target user. Ordinary visibility in another group does not grant administrative queue
rights. Regular users only see resources from their own groups and can always remove their own
stale queue/schedule entries after access is revoked.

In `Resource queue`, administrators get the `Admin clear current occupation` button. It releases the current occupant. If another entry is active immediately after the release, the resource transfers to that user automatically; otherwise the resource becomes free.

When the resource has queue or schedule entries, `Resource queue` also shows the `Admin release resource` button. It removes all queue/schedule entries for the selected resource, including the current occupation.

Admin commands also affect queue entries:

- [`/ban_user`](usage.md#cmd-ban-user) blocks a user and removes all of that user's queue/schedule entries.
- [`/delete_user`](usage.md#cmd-delete-user) deletes a user and removes that user's queue/schedule entries.
- [`/delete_resource`](usage.md#cmd-delete-resource) deletes a resource and all related entries.
- [`/update_resource`](usage.md#cmd-update-resource) with status `TemporarilyUnavailable` clears the resource entries and prevents new occupations/reservations while the resource is temporarily unavailable.
- Moving a resource or user to different groups atomically removes queue/schedule entries that are
  no longer visible. The change is rejected if that cleanup would affect a resource outside the
  actor's administered groups.

[`/occupy`](usage.md#cmd-occupy), [`/release`](usage.md#cmd-release), [`/release_all`](usage.md#cmd-release-all), [`/schedule`](usage.md#cmd-schedule), and [`/unschedule`](usage.md#cmd-unschedule) remain user-only commands. [`/queue`](usage.md#cmd-queue) and [`/soonest`](usage.md#cmd-soonest) are also available to administrators and [Local CLI](applications.md#booking-resources-cli).

## Limits and Automatic Events

Allowed occupation durations are managed with [`/occupation_durations`](usage.md#cmd-occupation-durations). When the duration list is enabled, [`/occupy`](usage.md#cmd-occupy) accepts only those values or an option number from the prompt.

Server settings limit queue usage:

- `queue.max_resources_per_user` limits how many different resources one user can occupy or reserve at the same time; `0` means unlimited.
- `queue.max_occupation_duration_minutes` limits one entry duration; `0` means unlimited and allows indefinite entries.

When [`/occupy`](usage.md#cmd-occupy), [`/schedule`](usage.md#cmd-schedule), or the matching menu actions reject a request, the reply names the reason. Typical cases include a start time in the past, an end time that is not after the start, an overlap with an existing reservation (the conflicting interval is included), a duration or resource-count limit (with the numeric cap), and mixing a fixed reservation with users who are already waiting for release. Slash commands and menus use the same reason text; menus may prefix it with `Error:`.

Server administrators change these values with [`/server_settings`](usage.md#cmd-server-settings).

When a finite occupation expires, its entry is removed automatically and the previous occupant receives a direct message. If another scheduled or after-release entry becomes active immediately after that, the new occupant also receives a direct start message.

When a future reservation reaches its start time while another entry is still active, the server ends the previous occupation because the scheduled reservation started. The previous occupant receives the end message and the scheduled user receives the start message.

For manual [`/release`](usage.md#cmd-release) or [`/release_all`](usage.md#cmd-release-all), the releasing user receives the command reply only; the server does not send that same user a separate occupation notification. Broader occupation notifications for manual occupy/release events depend on `occupation.notify`: `all`, `mine`, or `off`. In `mine` mode, users are notified only about events where they occupy, release, or receive/hand over a resource.
