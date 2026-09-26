# Changelog

## Unreleased

- Time refresh now defaults to every minute for new settings-page users.
- During Night colour, hour digits have a 3px red outline while minute digits
  stay filled red, making the rows easier to distinguish.
- Added a "Night colour" setting on the phone settings page: dims the face
  to red (digits, date, and the minute static) during a user-set 24-hour
  window, to cut blue/white light at night. Off by default.
- Added a "Time refresh" setting on the phone settings page: a 5-minute-grid-
  plus-shake behavior, or the default plain every-minute mode with no
  static and no shake needed (minute is always exact).
- Removed the diagnostic shake-log instrumentation: the on-watch `DayRecord`
  persist ring, the settings-page CSV export and "Diagnostic log" section,
  the hourly log lines, and `tools/pebble-log-to-csv.py`. It had served its
  purpose (confirming the 5-minute grid), and its per-minute persist write
  was the only measurable battery/flash cost in the app.

## 2026-09-06

- Quiet Time no longer falls back to an hourly repaint grid — the face
  redraws on the same 5-minute grid whether Quiet Time is on or not.
  Shake-to-wake stays blocked during Quiet Time as before.
- The phone settings-page diagnostic log no longer writes a placeholder row
  for hours later than "now" on the still-in-progress day (previously shown
  as `shakes` 0, `battery` blank) — only hours that have actually happened
  get a row.
- Shake-log CSV and phone settings-page log now list newest day/hour first
  instead of oldest.
- Date row is now a hardcoded solid white instead of a mid-blue tint, and
  the font is Quantico Bold instead of Michroma — a real bold weight instead
  of a faux-bold pixel smear (Michroma has no bold cut at all). Tried Orbitron
  Bold first, but its zero has a diagonal slash through it; Quantico doesn't.
  Sizes bumped too: gabbro 14px -> 16px, emery 21px -> 24px (20px where the
  locale's widest weekday/month strings don't fit at 24, was 18px).
- Minute row is now offset 15px left of the hour row for a slight stagger.
- Passive-minute static is dialed back (was a full 1000/1000 permille snowed,
  now 350/1000) so it reads as less agitated.
- Fixed a jump-backward glitch: when a shaken-exact reading expired, the
  display used to floor to the 5-minute grid mark *before* the exact minute
  (e.g. exact "12:33" -> static "12:30"), which read as the clock rewinding.
  It now holds the real minute the reveal just expired at (static "12:34"
  instead) and only re-syncs to the grid at the next scheduled tick.
- CI now builds the `.pbw` on every push to `main`.

### Gotcha: Quiet Time can turn on from a calendar event, not just the toggle

A watch that stops reacting to a wrist shake even though Quiet Time looks off
in Settings and no schedule is active is very likely picking up Pebble's
**"Quiet Time during Calendar events"** auto-trigger — a meeting on the phone's
calendar turns Quiet Time on for its duration without touching the manual
toggle or the scheduled window. Before treating "shake does nothing" as a
bug, check the phone calendar for an event covering the test window, and the
Quiet Time settings for that calendar-events option. The watch's own
`quiet_hour` column in the diagnostic CSV (Settings → Diagnostic log) confirms
this at a glance — if the hour you tested in shows `yes`, the app is
suppressing the shake correctly.
