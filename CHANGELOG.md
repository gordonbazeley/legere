# Changelog

## Unreleased

- Shake-log CSV and phone settings-page log now list newest day/hour first
  instead of oldest.
- Date row is now a hardcoded solid white instead of a mid-blue tint, and
  the font is Quantico Bold instead of Michroma — a real bold weight instead
  of a faux-bold pixel smear (Michroma has no bold cut at all). Tried Orbitron
  Bold first, but its zero has a diagonal slash through it; Quantico doesn't.
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
