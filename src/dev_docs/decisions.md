# legere — Key Decisions

## The 5-minute grid is an identity choice, not a power optimisation

**Chose:** Keep the passive 5-minute repaint grid (hourly during Quiet Time) as a
defining feature of the face — "the minutes are soft unless you ask" — and stop
framing legere as a "lowest-power" watchface.

**Why it changed:** The original thesis (commit `89c0224`, "low-power TTMM-style
face") assumed the redraw cadence was a meaningful battery lever. Researched
against Pebble Time 2 hardware (185 mAh; 21 days ≈ 367 µA average): the minute
*wake* is nearly free (~10⁻⁵ mAh), and skipping 4 redraws in 5 saves an
estimated **~6–26 µA — under one day over three weeks**, below telemetry noise.
The real drains are motion-activated backlight, notifications/vibration,
HR sampling, and BLE reconnect churn — none of which a watchface controls.

**Trade-off:** legere can no longer claim battery life as its headline. It keeps
the mechanic because the interaction (a calm, approximate readout you can sharpen
with a shake) is liked on its own merits — five commits of refinement went into
the shake path. The 21-day target stays as "should hit it anyway by doing
nothing wasteful," not a promise.

**Sources:** research captured in the grill session, Sept 2026 — key ones:
repebble.com "Pebble Time 2 Is In Mass Production", developer.repebble.com
"Conserving Battery Life", help.repebble.com battery article,
forum.repebble.com/t/hows-your-battery-life/576, cnx-software SF32LB52J writeup.

## No A/B control build to measure the grid

**Chose:** Do not build a `REDRAW_EVERY_MINUTE` control variant or run a
multi-day A/B comparison.

**Why:** Follows from the decision above — if the grid isn't a power feature
there is nothing to prove. A clean A/B is days of disrupted wear to measure an
effect smaller than the day-to-day variance from notifications and temperature.

**Trade-off:** We will never have a hard in-house number for the grid's saving.
Acceptable; the estimate is enough.

## Freshness signalled two ways: month colour + minute brightness

**Chose:** Colour is the month's job (blue = passive, red = exact); brightness is
the minute's job (`GColorLightGray` = passive, `GColorWhite` = exact). The hour
row is always `GColorDarkGray` and never changes.

**Why:** Two orthogonal signals are easier to learn than one colour meaning two
things. The month colour alone (the pre-existing signal) is a tiny cue a stranger
won't decode; dimming the minutes makes "not locked in" legible at a glance
without adding an element or a glyph. The hour is genuinely always exact, so
changing it would signal something untrue.

**Trade-off:** The passive→exact minute change is grey→white, which is subtle
when glancing at the face in isolation (obvious side by side). Accepted — a
louder option (blue minutes) was mocked and rejected as making colour do double
duty. Costs nothing: reuses the existing `prv_set_ink` palette poke, zero power.

**Rejected:** trailing `~`/`+` glyph (needs a sprite, eats horizontal room);
whole-time-snaps-brighter (lies about the hour); a "very stale" state for Quiet
Time (risks a `07:00`-at-07:58 face looking frozen — the backlight-forces-exact
path already covers a real look).

## Deep row overlap for larger digits (`DIGIT_GAP = -20` on emery)

**Chose:** Draw the minute row overlapping the hour row by 20px, regenerate
`digits_lg` at 93×103 (was 85×94), draw the hour row `GColorDarkGray`.

**Why:** The emery digit block was at zero vertical slack — digits couldn't grow
without reclaiming space. A negative gap frees it; the dark hour row keeps the
overlapping lighter minute ink reading as "in front".

**Trade-off:** Digit foot/head collision on ~30–40% of times (e.g. `22:57` — the
"2" foot under the "5"/"7" top bars). Deliberately accepted for the size gain;
the grey/white contrast keeps it legible. Committed `67ae9f5`, not yet validated
on hardware (dark-grey-on-black legibility unlit is the open risk).

## Digit edge cleanup via `STROKE_THIN` (`gen-digits.sh`)

**Chose:** `STROKE_THIN=8` (a light uniform erode in 400pt-render px) when
regenerating the sprite sheets.

**Why:** Alfa Slab One's "5" arm-to-bowl gap and "7" top notch are thin
*exterior* silhouette features, not enclosed counters, so `punch-holes.py`'s
counter-grow never touched them — downscaling + alpha quantise crushed them to
near-invisible. A small overall erode opens both without blunting corners (4's
apex, 0/6/8/9 counters checked). Committed `27ab5ba`.

## Launcher icon: white glyph + 1px black keyline

**Chose:** `resources/images/icon.png` is a white feather with a 1px black
outline, not plain white ink.

**Why:** The 4.x launcher preserves the icon's transparency and themes the row
background itself — it does *not* invert per-row on emery/gabbro (inversion is an
old B&W-aplite feature). A plain white icon is invisible on the light unselected
row and visible only when selected. The keyline makes it read on any background.

**Trade-off:** 1px of the glyph body is eaten by the outline; the feather detail
is a touch heavier at 25×25. Fine.

## Config from system preferences only — no custom settings UI

**Chose:** 12/24h from `clock_is_24h_style()`, locale from the system (once
wired — see `todo.md`). No user-facing config for colours, fields shown, or the
exact/passive default.

**Why:** The user already set these in Pebble settings; a settings page just to
duplicate them is friction. Keeps the phone companion to the log export + a
ko-fi link.

**Trade-off:** Someone who wants, e.g., always-exact or a different accent colour
can't have it. Acceptable for v1.

## Locale support: Latin-script only

**Chose:** Call `setlocale(LC_ALL, i18n_get_system_locale())`, expand the date
font glyph subset to Latin-1 + common accents, use non-ASCII-safe uppercasing.
Covers FR/DE/ES/IT/PT/NL. Not full i18n.

**Why:** The store is Europe-heavy; `setlocale` + a ~20–30 glyph subset bump is
contained, and it's the difference between "German users see MÄR" and "German
users see a tofu box or English". Non-Latin scripts would blow the font cost and
likely break the layout for a date line.

**State:** not implemented — `strftime` currently runs in the C locale (always
English) and the font is subset to `[A-Z0-9 ]`.

## Diagnostic instrumentation is temporary

**Chose:** The `DayRecord` ring buffer, AppMessage export, `pebble-log-to-csv.py`,
and the `APP_LOG` "row"/"shake-wake" lines all get removed at store launch. A
temporary hourly battery-% sample is added to `DayRecord` in the meantime.

**Why:** With the grid reframed as identity (not power), the log has nothing left
to prove. It stays only as a "is legere doing something dumb on my own wrist"
check while the face is finished.

**Trade-off:** Message keys (`RequestLog`, `Year`, `Mon`, `Mday`, `Shakes`,
`QuietMask`, `Done`) and the `configurable` capability come back out of
`package.json` when the export goes — unless the ko-fi settings page keeps a
companion around, in which case the export code goes but the plumbing stays.

## Platforms: emery + gabbro only

**Chose:** `targetPlatforms` is `["emery", "gabbro"]`; B&W paths dropped.

**Why:** `89c0224` — both are colour with a 512 B glyph cache; supporting the
original aplite/basalt/chalk/diorite meant a second small sprite sheet and B&W
branches for no user (the modern hardware is emery/gabbro/flint).

**Trade-off:** No Pebble Classic / Time / Time Round support. `flint` (Pebble 2
Duo, B&W) is also unsupported — would need the B&W path back.
