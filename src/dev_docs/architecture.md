# legere — Architecture

## What it is

A calm, deliberate Pebble watchface. Bold overlapping slab numerals for the
time, always shown exact, redrawn every minute. A date row underneath is a
plain white constant.

The face previously floored the minute to a 5-minute grid and rendered it as
TV static until a shake revealed the exact time, with a `RedrawMode` setting
to opt into always-exact instead — removed 2026-09-28 for too much complexity
for too little benefit; see `decisions.md`.

It is **not** a "lowest-power" face as its headline (that was an early framing —
see `decisions.md` → "The 5-minute grid is an identity choice, not a power
optimisation", now historical). It is a low-*fuss* face that also happens to
do nothing wasteful.

Single-file watch app. The phone companion is a settings page with a "Night
colour" control, saved with one Save button, plus info + ko-fi link. The
setting persists on the watch; see `decisions.md`.

```
tick (every minute, from the OS)
  └── mark dirty, repaint
        prv_digits_update_proc  (s_digits_layer)
          ├── hour digits  (dark grey by day, solid red at night)
          └── minute digits (white by day, hollow red outline at night)
        prv_date_update_proc    (s_date_layer — only marked dirty on a date rollover)
          └── date row     (weekday / day / month; whole row a constant white)
```

## Rendering

### Digits are pre-rendered bitmaps

A font can't be rasterised large enough on-watch (per-glyph cache limit), so the
digits ship as a **sprite sheet**: one PNG of 10 fixed-width slots, sliced into
`s_digit[0..9]` sub-bitmaps at load. Night mode uses a matching pre-rendered
3px-outline sheet, avoiding repeated offset blits. `tools/gen-digits.sh` regenerates the
sheets from `resources/fonts/AlfaSlabOne-Regular.ttf` (see that script's header
for the full pipeline — shared-baseline crop, counter-hole punching, alpha
quantisation, per-digit right-alignment).

- `emery`  → `IMAGE_DIGITS_LG` — slot 93×103, rows overlap −20px (see below)
- `gabbro` → `IMAGE_DIGITS`    — slot 73×80 (260×260 round)

### Layout (`prv_window_load`)

`PBL_IF_ROUND_ELSE` throughout. Emery stacks top-down from a fixed `top_margin`;
gabbro anchors the date row to the bottom (`bot_margin`) and pulls the digit
grid in from both edges so its corners clear the bezel.

| constant | emery | gabbro | meaning |
|---|---|---|---|
| `PAD` | 6 | 20 | screen-edge padding |
| `DIGIT_GAP` | **−20** | 6 | px between hour and minute rows; negative = deliberate overlap |
| `DIGIT_BAND_BOT_GAP` | 5 | 6 | px between minute row and date row |
| `top_margin` | 5 | 28 | |
| `bot_margin` | n/a | 32 | gabbro only — emery derives `s_date_top` without it |

On emery `s_date_top` is derived from the same terms as the block-centering
expression, so `block_top` collapses to `top_margin` and any vertical slack
falls between the digit block and the date row.

### The overlap (`DIGIT_GAP = -20` on emery)

The minute row is drawn *over* the hour row, overlapping by 20px, so the digits
can be regenerated ~10% larger in the same vertical budget. The hour row is
`GColorDarkGray` and the minute row lighter, so the overlapping white/grey
minute ink reads as clearly "in front". Committed in `67ae9f5`. The digit
foot/head collision this causes is intentional and accepted.

### Freshness

The minute is always exact, so there's no separate freshness signal to carry.
Hour and minute digits are tinted by `prv_set_ink()` poking the sprite sheet's
palette in-place before each blit — `GColorDarkGray`/`GColorWhite` by day,
`NIGHT_INK` (solid hour, hollow outline minute) at night.

## Time model

- `prv_display_hour()` — 12/24h from `clock_is_24h_style()` (system preference).
- The minute shown is always `t->tm_min` — no floor, no separate passive state.

## Repaint schedule (`prv_tick_handler`)

The OS wakes the app every minute for its own clock; legere repaints on every
tick.

## Power model

The dominant Pebble Time 2 battery drains, ranked (see `decisions.md` for the
research): motion-activated backlight, vibration/notification volume, HR/health
sampling, BLE reconnect churn, then pathological watchface behaviour
(`SECOND_UNIT`, per-tick animation, timed network fetches). A minute-updating
static face with no animation is **not** in the top five.

legere's controllable levers, in full:
1. Load bitmaps/fonts once in `prv_window_load`, never per-redraw — **done**.
2. Keep `graphics_text_layout_get_content_size` (`prv_measure`) off the redraw
   path — **done** (only called in `prv_window_load`).
3. `mark_dirty` only the layer that changed, not the whole window — **done**
   (`s_digits_layer` / `s_date_layer` are disjoint; the date is repainted only
   on a rollover).
4. Cache the formatted time/date strings, re-render on change only — **done**
   (date strings keyed on `tm_mday` via `s_str_mday`; `hour_str`/`min_str` moved
   into the resource-failure fallback).

The face redraws every minute; per `decisions.md` this was already estimated
to cost only ~6–26 µA relative to a 5-minute cadence — a rounding error next
to backlight/BLE/HR drains.

## Battery guidance (for the store listing)

legere's own draw is negligible; a Pebble Time 2 owner's battery life is set
almost entirely by system settings. What to tell users in the listing:

1. **Turn off motion-activated backlight.** The single biggest lever — forum
   users report ~14 → 20+ days from this alone. (Settings → Backlight → Motion.)
2. **Set heart-rate background sampling to 30 min or hourly, not 10 min.** No
   figure is published, but the estimate is 10-min costs ~0.3–1 mAh/day
   (~15–45 µA) more than 30-min ≈ 1–4 fewer days on a 20–30 day baseline.
   PebbleOS already skips HR samples when the watch lies flat and never samples
   on the charger, so the overnight cost of either setting is zero — the gap is
   only ~14 waking wrist-hours/day. "HR during activities" is a separate setting
   that goes continuous during a detected walk/run regardless of the background
   interval. (Source: PebbleOS `src/fw/services/activity/activity.c`,
   `activity_private.h`; help.repebble.com battery article.)
3. **Keep notification volume modest** — each notification wakes the screen and
   often the backlight; vibration adds actuator draw.
4. **Keep Bluetooth connected and stable** — steady connected draw is ~50 µA;
   it's the disconnect/reconnect churn that's expensive.
5. Run reasonably current PebbleOS — early firmware had battery bugs.

With all of that, ~21 days is realistic and legere does nothing to stop it. None
of it is legere-specific; it applies to any minimal watchface.

## Diagnostic log — removed 2026-09-07

A `DayRecord` persist ring (per-hour shake counts, Quiet Time bitmask, hourly
battery %), an AppMessage export to the settings page, and hourly `APP_LOG`
"row" lines + `tools/pebble-log-to-csv.py` were carried while the 5-minute grid
question was open. Removed once that was settled — see `decisions.md`. An audit
for battery/flash cost found the sampler's per-minute `persist_write_data` was
the only non-trivial drain in the codebase; nothing replaced it. The
remaining message keys (`NightEnabled`/`NightStart`/`NightEnd`, the night
colour setting — `RedrawMode` was removed 2026-09-28, see `decisions.md`) are
inbox-only — the watch never sends anything to the phone.

## Platforms

`emery` + `gabbro` only (both colour, 512 B glyph cache). The B&W code paths and
the small sprite sheet from the original design were dropped in `89c0224`.
