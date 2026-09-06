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
(`prv_snow_permille()` 1000→0), so the digits surface out of the noise; the last
frame is clean white. The ramp is symmetric — when the exact reading expires it
plays in reverse (`s_shimmer_out`, 0→1000) so the minute dissolves back into
static instead of a one-frame cut. The hour row is always solid `GColorDarkGray`
and never flickers. The date row does **not** carry a freshness cue — whole line is a
constant mid blue (`DATE_COLOR` = `GColorVividCerulean`).

**Why:** An early version put the freshness cue on the month colour (blue
passive / red exact). Dropped: it's a tiny corner cue a stranger won't decode,
and the static already says it unmistakably — "this signal isn't locked in"
reads instantly, and the shake→lock-on gives the interaction a satisfying
payoff. An earlier plan (grey minutes → white minutes) was too subtle glancing
at the face in isolation. The hour is genuinely always exact, so it stays solid.
Colouring the whole date row (not just white text) also just reads better on
the unlit transflective LCD, where the old white day-of-month was faint.

**How:** `prv_staticify(ctx, rect, permille)` works at the framebuffer level
(`graphics_capture_frame_buffer`) — the minute glyphs are drawn solid white,
then each fully-opaque white pixel inside the minute-row rect is, with
probability `permille`/1000, replaced by a random `SNOW[]` entry (white → light
grey → dark grey → black — a real dropout spread, not just greys, so it carries
on the unlit reflective LCD). The anti-aliased glyph edges (not pure white) are
left alone, so the digit keeps a clean outline around the noise. `permille` is
1000 while passive; the lock-on ramp (`prv_snow_permille()`, `SHIMMER_FRAMES`
redraws) steps it 1000→0 so the digits emerge from the noise. ~19k pixel writes
per passive redraw — negligible at the 5-minute cadence; the ramp is a one-shot
burst on an explicit shake.

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

**State:** implemented, pending a hardware pass. `setlocale` wired in `prv_init`;
`prv_utf8_upper` does non-ASCII uppercasing; date font is `Quantico-Bold.ttf`
subset to `[A-Z0-9 .À-Öß]` (Quantico's cmap has no gaps in that range).
Emery picks 24px or 20px at load
(`prv_pick_date_font`) by measuring the locale's widest weekday/month strings —
FR/ES overrun 24px ("SEPT." + accented period-weekdays), the rest keep 24.
Untested with a real non-English language pack — the emulator can't install one.

## Diagnostic instrumentation is temporary

**Chose:** The `DayRecord` ring buffer, AppMessage export, `pebble-log-to-csv.py`,
and the `APP_LOG` "row"/"shake-wake" lines all get removed at store launch. A
temporary hourly battery-% sample (`DayRecord.battery[24]`, integer percent,
`0xFF` = no sample) is in `DayRecord` in the meantime, surfaced as the CSV
`battery` column.

**Why:** With the grid reframed as identity (not power), the log has nothing left
to prove. It stays only as a "is legere doing something dumb on my own wrist"
check while the face is finished.

**Trade-off:** Message keys (`RequestLog`, `Year`, `Mon`, `Mday`, `Shakes`,
`Battery`, `QuietMask`, `Done`) and the `configurable` capability come back out of
`package.json` when the export goes — unless the ko-fi settings page keeps a
companion around, in which case the export code goes but the plumbing stays.

## The phone-side CSV skips hours with no data, instead of dumping the whole day

**Chose:** `openPage()` in `src/pkjs/index.js` now skips any hour whose
`battery` sample is still `255` before writing CSV rows, instead of writing
all 24 hours of every persisted day unconditionally.

**Why:** `battery[24]` is sampled every minute (`prv_tick_handler`), so `255`
means that hour of the day hasn't happened yet — true for the rest of
today's in-progress record, which the export always includes (see above).
Before this, opening the settings page any time before 23:00 produced a CSV
with a placeholder row for every hour later than "now" — `shakes` 0, `battery`
blank, `quiet_hour` `no` — indistinguishable from a real quiet hour with no
shakes. Writing the whole (mostly future) day's rows in one pass on every
`openPage()` call was also the actual "write log lines … for the whole day
at one time" behaviour this was meant to avoid — the watch-side `APP_LOG`
path (above) already only ever writes one real row per finished hour.

**Trade-off:** None identified — the skip only removes rows that carried no
real information; nothing downstream (the settings page's textarea/Share,
`tools/pebble-log-to-csv.py`) parses the CSV shape, so nothing depended on the
old placeholder rows being present.

## Platforms: emery + gabbro only

**Chose:** `targetPlatforms` is `["emery", "gabbro"]`; B&W paths dropped.

**Why:** `89c0224` — both are colour with a 512 B glyph cache; supporting the
original aplite/basalt/chalk/diorite meant a second small sprite sheet and B&W
branches for no user (the modern hardware is emery/gabbro/flint).

**Trade-off:** No Pebble Classic / Time / Time Round support. `flint` (Pebble 2
Duo, B&W) is also unsupported — would need the B&W path back.
