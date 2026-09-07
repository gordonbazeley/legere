# legere — Key Decisions

## The 5-minute grid is an identity choice, not a power optimisation

**Chose:** Keep the passive 5-minute repaint grid — the same cadence during
Quiet Time as any other time, no hourly fallback (see "Quiet Time keeps the
same 5-minute grid" below) — as a defining feature of the face — "the minutes
are soft unless you ask" — and stop framing legere as a "lowest-power"
watchface.

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

## Quiet Time keeps the same 5-minute grid, no hourly fallback

**Chose:** Repaint on the 5-minute grid during Quiet Time too, instead of
falling back to hourly (`prv_tick_handler`'s `step` collapses from
`quiet ? 60 : 5` to a flat `5`).

**Why:** Follows directly from the decision above — once the grid is framed
as identity rather than a power lever, there's no reason to let the face go
up to 59 minutes stale specifically while Quiet Time is on. The static/
shimmer repaint (`prv_staticify`'s framebuffer capture) is the more expensive
part of a redraw here, but it already runs at the 5-minute cadence during
normal hours without being a measurable concern (see above) — running it
5x more often during Quiet Time's typically-asleep hours doesn't change that
math. Shake-to-wake stays blocked during Quiet Time regardless — that's the
OS's own backlight behaviour, untouched by this handler — so this only
affects how stale the face looks if glanced at without waking the backlight.

**Trade-off:** None identified beyond the redraw-cost point above, which the
existing power research already covers.

## No A/B control build to measure the grid

**Chose:** Do not build a `REDRAW_EVERY_MINUTE` control variant or run a
multi-day A/B comparison.

**Why:** Follows from the decision above — if the grid isn't a power feature
there is nothing to prove. A clean A/B is days of disrupted wear to measure an
effect smaller than the day-to-day variance from notifications and temperature.

**Trade-off:** We will never have a hard in-house number for the grid's saving.
Acceptable; the estimate is enough.

## Passive minutes render as TV static ("no signal")

**Chose:** When the reading is passive (floored), the minute digits are filled
with random `SNOW[]` "snow" (white → black) — a dead-channel look — instead of
solid ink. A shake resolves them with a lock-on ramp: `SHIMMER_FRAMES` redraws
`SHIMMER_MS` apart, each snowing a smaller fraction of the ink
(`prv_snow_permille()` `PASSIVE_SNOW_PERMILLE`→0), so the digits surface out of
the noise; the last frame is clean white. The ramp is symmetric — when the
exact reading expires it plays in reverse (`s_shimmer_out`) so the minute
dissolves back into static instead of a one-frame cut. The hour row is always
solid `GColorDarkGray` and never flickers. The date row does **not** carry a
freshness cue — whole line is a constant `GColorWhite` (`DATE_COLOR`).

**Why:** An early version put the freshness cue on the month colour (blue
passive / red exact). Dropped: it's a tiny corner cue a stranger won't decode,
and the static already says it unmistakably — "this signal isn't locked in"
reads instantly, and the shake→lock-on gives the interaction a satisfying
payoff. An earlier plan (grey minutes → white minutes) was too subtle glancing
at the face in isolation. The hour is genuinely always exact, so it stays solid.
Colouring the whole date row (not just white text) also just reads better on
the unlit transflective LCD, where the old white day-of-month was faint. The
whole-row tint itself later went from a mid blue (`GColorVividCerulean`) to
plain white — simplest thing that reads, no real reason to keep a second
accent colour once it wasn't doing any signalling work.

**How:** `prv_staticify(ctx, rect, permille)` works at the framebuffer level
(`graphics_capture_frame_buffer`) — the minute glyphs are drawn solid white,
then each fully-opaque white pixel inside the minute-row rect is, with
probability `permille`/1000, replaced by a random `SNOW[]` entry (white → light
grey → dark grey → black — a real dropout spread, not just greys, so it carries
on the unlit reflective LCD). The anti-aliased glyph edges (not pure white) are
left alone, so the digit keeps a clean outline around the noise. `permille` is
`PASSIVE_SNOW_PERMILLE` while passive (currently 350 — dialed back from a full
1000 across a few rounds of tuning, since 1000 read as more agitated than
intended); the lock-on ramp (`prv_snow_permille()`, `SHIMMER_FRAMES` redraws)
steps it `PASSIVE_SNOW_PERMILLE`→0 so the digits emerge from the noise. ~19k
pixel writes per passive redraw — negligible at the 5-minute cadence; the ramp
is a one-shot burst on an explicit shake.

**Trade-off:** Static is visually *agitated* — arguably against the "calm"
identity. Accepted deliberately (user asked for it); the snow is frozen between
5-minute repaints (re-randomised only when the clock ticks the grid or during a
lock-on), so it doesn't literally flicker at rest. The framebuffer approach ties
the effect to the 8-bit colour format (fine for emery + gabbro).

**Exact reading expires at the next minute tick** (`s_exact_hour`/`s_exact_min`
in `prv_tick_handler`), not at the next 5-minute grid tick. A shake shows the
true minute; the moment the clock rolls past it the digits are stale, so the
face returns to the static then (via the reverse ramp) rather than holding a
clean-but-wrong reading for up to 5 minutes (up to ~59 in Quiet Time). Costs one
extra `SHIMMER_FRAMES` burst per manual refresh, a minute later — the OS already
wakes the app every minute, so no extra wake. During a walk the tap path now
cycles static → ramp → clean → ramp → static roughly once a minute instead of
sitting clean between grid ticks; accepted (the reading genuinely is only fresh
right after the tap).

**Rejected:** grey-vs-white minute brightness (too subtle); blue minutes (makes
colour do double duty with the month); trailing `~`/`+` glyph (needs a sprite,
eats horizontal room); whole-time-snaps-brighter (lies about the hour); a
stronger "very stale" state for Quiet Time (risks a `07:00`-at-07:58 face looking
frozen — the backlight-forces-exact path already covers a real look).

## An expiring exact reading holds the real minute, doesn't floor backward

**Chose:** `s_passive_min` is tracked as state rather than recomputed as
`prv_floor5(current real minute)` on every passive redraw. `prv_tick_handler`
keeps it grid-aligned during normal operation (writing `tick_time->tm_min` at
ticks where that's already a 5-minute mark — the same result as the old
floor), but the moment an exact reading expires it's pinned to that real,
unfloored minute instead, only re-syncing to the grid at the next scheduled
tick.

**Why:** The old `prv_floor5(t->tm_min)`-every-redraw approach meant the
instant a shaken-exact reading expired, the passive display floored to the
5-minute mark *before* the exact minute — e.g. a shake at 12:33 (exact "12:33")
expiring one tick later at 12:34 showed static "12:30", since `floor5(34) ==
30`. That reads as the clock rewinding, which undermines the whole "static =
imprecise, not wrong" premise the freshness signal depends on (see "Passive
minutes render as TV static" above). Holding "12:34" instead — the truthful
minute the reveal just expired at — never contradicts a minute the user
already saw exact.

**Trade-off:** The passive minute can very briefly (until the next 5-minute
mark) show a value that isn't itself a multiple of 5, which is a small
departure from "minutes are always shown soft" as a strict invariant. Accepted
— it only happens right after a shake, already snowed over, and the
alternative (the backward jump) was the actually-confusing behaviour.

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

## Date font: Michroma → Orbitron Bold → Quantico Bold

**Chose:** `Quantico-Bold.ttf` at 16px (gabbro) / 24px, dropping to 20px where
the locale is too wide (emery) — see "Locale support" below for the glyph
subset. Went through two prior fonts to get here.

**Why it changed twice:** Michroma (the original date font) has no bold cut at
all — it ships one weight — so the row needed a faux-bold trick: `prv_draw_cell`
redrew the glyph at a 1-2px offset in both axes to fake a heavier stroke. That
smear approach has real limits (an x-only smear thickens vertical strokes but
not horizontal ones; a full 2px smear in both axes blurs letters together;
fractional weights like "1.5px" can only be faked with a lighter-shade fringe
pixel, not a true half-pixel offset) that a font with a genuine bold weight
sidesteps entirely. Orbitron Bold was the first real-bold candidate tried —
same geometric/futuristic character as Michroma, OFL-licensed, ships weights
400-900 — but its zero has a diagonal slash through it in every weight
(checked Regular and Bold), which reads oddly in the day-of-month digits.
Quantico Bold has a real bold weight, no slashed zero, and a cmap with no gaps
against the full Latin-1 accented block (a superset of what
FR/DE/ES/IT/PT/NL need) — so no `characterRegex` changes were needed beyond
what Orbitron had already required (dropping Ð/Ø/Þ, which Orbitron's cmap
lacked and which none of the supported locales use anyway).

**Trade-off:** `prv_draw_cell` is back to a single `graphics_draw_text` call —
simpler than it was, no smear/fringe code to carry. Quantico is a slightly
plainer geometric face than Orbitron; accepted for the correct zero glyph.

## Config from system preferences only — no custom settings UI

**Chose:** 12/24h from `clock_is_24h_style()`, locale from the system (once
wired — see `todo.md`). No user-facing config for colours or fields shown.

**Why:** The user already set these in Pebble settings; a settings page just to
duplicate them is friction. Keeps the phone companion to the log export + a
ko-fi link.

**Trade-off:** Someone who wants, e.g., a different accent colour can't have
it. Acceptable for v1.

**Superseded in part** by "Time refresh setting: 5-minute-plus-shake vs.
every-minute" below — the exact/passive default is now user-configurable
after all, but only as an explicit stopgap for the touch-less state of
things, not a reversal of the general "no config UI" stance.

## Time refresh setting: 5-minute-plus-shake vs. every-minute

**Chose:** A real setting on the phone settings page — `MESSAGE_KEY_RedrawMode`
(int, persisted at `PERSIST_KEY_REDRAW_MODE` via `persist_write_bool` /
`s_every_minute`) — toggling between the existing 5-minute-grid-plus-shake
behaviour (default) and a plain every-minute mode: the minute is always shown
exact, redrawn every tick, no static and no shake needed at all
(`prv_snow_permille()` returns 0 outright when `s_every_minute`; `disp_min` in
`prv_digits_update_proc` takes the exact branch either way; `prv_tick_handler`
drops its grid `step` to 1). Saved via the standard Pebble config-page handoff
(`settings.html`'s Save button navigates to `pebblejs://close#<json>`;
`index.js`'s `webviewclosed` listener relays it as an AppMessage).

**Why:** Touch input would be the natural way to ask a watchface for the
exact time on demand — tap the screen, see it — but Pebble currently
restricts `TouchService` to watchapps, not watchfaces (see the touch-cost
discussion this came out of). Shake/tap-to-reveal is the workaround for that
restriction; not everyone wants the friction of a deliberate gesture just to
read the exact minute, so this setting gives the alternative: give up the
soft-minute identity and static/shake mechanic entirely, get a normal
always-accurate clock instead.

**Trade-off:** This is explicitly temporary — **remove this setting** (and
the `RedrawMode` message key, `PERSIST_KEY_REDRAW_MODE`, `s_every_minute`, and
the settings-page section) once touch is enabled for watchapps and legere can
just let a screen tap reveal the exact minute, matching how shake/tap already
work. Until then it's one more piece of state and one more settings-page
section for what should eventually be unnecessary. Also reopens "no custom
settings UI" as a v1 stance — accepted, since the alternative (no way to opt
out of the static/shake mechanic at all) is worse for someone who just wants
a normal watch.

## Locale support: Latin-script only

**Chose:** Call `setlocale(LC_ALL, i18n_get_system_locale())`, expand the date
font glyph subset to Latin-1 + common accents, use non-ASCII-safe uppercasing.
Covers FR/DE/ES/IT/PT/NL. Not full i18n.

**Why:** The store is Europe-heavy; `setlocale` + a ~20–30 glyph subset bump is
contained, and it's the difference between "German users see MÄR" and "German
users see a tofu box or English". Non-Latin scripts would blow the font cost and
likely break the layout for a date line.

**State:** implemented, pending a hardware pass. `setlocale` wired in `prv_init`;
`prv_utf8_upper` does non-ASCII uppercasing; date font is `Quantico-Bold.ttf`
subset to `[A-Z0-9 .À-Öß]` (Quantico's cmap has no gaps in that range).
Emery picks 24px or 20px at load
(`prv_pick_date_font`) by measuring the locale's widest weekday/month strings —
FR/ES overrun 24px ("SEPT." + accented period-weekdays), the rest keep 24.
Untested with a real non-English language pack — the emulator can't install one.

## Diagnostic instrumentation — removed 2026-09-07

**Was:** A `DayRecord` persist ring (per-hour shake counts, a Quiet Time
bitmask, hourly battery %), an AppMessage export to the settings page, hourly
`APP_LOG` "row" lines + `pebble-log-to-csv.py`, and the settings-page
"Diagnostic log" section. Carried to check whether the 5-minute grid was worth
keeping vs. redrawing every minute, and to spot the face misbehaving on the
author's own wrist.

**Removed because:** the grid question is settled (identity, not power — see
above), so the log had nothing left to prove. An audit for battery/flash cost
then found the instrumentation was the *only* non-trivial drain in the
codebase: `prv_tick_handler` did a 56-byte `persist_write_data` every time the
reported battery % dropped a step (~10–100 writes/day) plus one per manual
refresh — real energy, and it burns the persist region's ~100k-cycle wear
budget. Shipping code was otherwise clean.

**Kept:** shake-to-wake (`accel_tap_service`), the "Time refresh" setting
(`RedrawMode`, now the only message key), the ko-fi settings page, and the
`configurable` capability. The watch's AppMessage channel is inbox-only now
(`app_message_open(..., 0)`). Old persist keys (190, 200–213) on installed
watches are left in place — nothing reads them, no migration worth writing
pre-launch. The phone remembers the Time refresh choice in `localStorage`
rather than reading it back from the watch, since the watch→phone export path
is gone.

## Platforms: emery + gabbro only

**Chose:** `targetPlatforms` is `["emery", "gabbro"]`; B&W paths dropped.

**Why:** `89c0224` — both are colour with a 512 B glyph cache; supporting the
original aplite/basalt/chalk/diorite meant a second small sprite sheet and B&W
branches for no user (the modern hardware is emery/gabbro/flint).

**Trade-off:** No Pebble Classic / Time / Time Round support. `flint` (Pebble 2
Duo, B&W) is also unsupported — would need the B&W path back.
