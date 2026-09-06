# legere — Architecture

## What it is

A calm, deliberate Pebble watchface. Bold overlapping slab numerals for the
time; the **minutes are shown soft** (floored to a multiple of 5) unless the
user asks for the exact minute with a wrist shake. A date row underneath
carries a freshness colour signal.

It is **not** a "lowest-power" face as its headline (that was an early framing —
see `decisions.md` → "The 5-minute grid is an identity choice, not a power
optimisation"). It is a low-*fuss* face that also happens to do nothing
wasteful.

Single-file watch app, no phone companion logic beyond a diagnostic log export.

```
tick (every minute, from the OS)
  ├── advance the day-record ring, sample Quiet Time flag
  ├── on the 5-minute grid (hourly during Quiet Time): mark dirty, passive repaint
  │
shake / tap  ──┐
backlight on ──┼── force an exact repaint (s_exact = true), unless nothing would change
               │
        prv_digits_update_proc  (s_digits_layer)
          ├── hour digits  (bitmap blits, GColorDarkGray, always)
          └── minute digits(bitmap blits, LightGray when passive / White when exact)
        prv_date_update_proc    (s_date_layer — only marked dirty on rollover / s_exact flip)
          └── date row     (weekday / day / month; month blue=passive, red=exact)
```

## Rendering

### Digits are pre-rendered bitmaps

A font can't be rasterised large enough on-watch (per-glyph cache limit), so the
digits ship as a **sprite sheet**: one PNG of 10 fixed-width slots, sliced into
`s_digit[0..9]` sub-bitmaps at load. `tools/gen-digits.sh` regenerates the
sheets from `resources/fonts/AlfaSlabOne-Regular.ttf` (see that script's header
for the full pipeline — shared-baseline crop, counter-hole punching, alpha
quantisation, per-digit right-alignment).

- `emery`  → `IMAGE_DIGITS_LG` — slot 93×103, rows overlap −20px (see below)
- `gabbro` → `IMAGE_DIGITS`    — slot 58×62 (sized for a 180 round; gabbro is
  actually 260×260 — layout retune pending, see `todo.md`)

### Layout (`prv_window_load`)

`PBL_IF_ROUND_ELSE` throughout. Emery stacks top-down from a fixed `top_margin`;
gabbro anchors the date row to the bottom (`bot_margin`) and pulls the digit
grid in from both edges so its corners clear the bezel.

| constant | emery | gabbro | meaning |
|---|---|---|---|
| `PAD` | 6 | 18 | screen-edge padding |
| `DIGIT_GAP` | **−20** | 4 | px between hour and minute rows; negative = deliberate overlap |
| `DIGIT_BAND_BOT_GAP` | 5 | 4 | px between minute row and date row |
| `top_margin` | 5 | 16 | |
| `bot_margin` | 2 | 22 | |

On emery `s_date_top` is derived from the same terms as the block-centering
expression, so `block_top` collapses to `top_margin` and any vertical slack
falls between the digit block and the date row.

### The overlap (`DIGIT_GAP = -20` on emery)

The minute row is drawn *over* the hour row, overlapping by 20px, so the digits
can be regenerated ~10% larger in the same vertical budget. The hour row is
`GColorDarkGray` and the minute row lighter, so the overlapping white/grey
minute ink reads as clearly "in front". Committed in `67ae9f5`. The digit
foot/head collision this causes is intentional and accepted.

### Two freshness signals: month colour + minute static

- **Month colour**: `GColorElectricBlue` when the reading is passive (floored),
  `GColorRed` when it was just refreshed to the exact minute.
- **Minute digits**: rendered as TV-static "snow" while passive, resolving to
  solid `GColorWhite` when exact. A shake plays a short lock-on flicker first
  (`SHIMMER_FRAMES` × `SHIMMER_MS`, ~275 ms) — fresh snow each frame, then clean.
- **Hour row**: always solid `GColorDarkGray`, never flickers — the hour is
  always exact, so signalling anything on it would be a lie.

Hour/minute digits are tinted by `prv_set_ink()` poking the sprite sheet's
palette in-place before each blit. The static is `prv_staticify()`: after the
minute glyphs are drawn solid white, it captures the framebuffer
(`graphics_capture_frame_buffer`, emery/gabbro 8-bit) and replaces every
fully-opaque white pixel in the minute-row rect with a random
`GColorLightGray`/`GColorWhite`. Anti-aliased edge pixels (not pure white) are
left, so the glyph keeps a clean outline. `s_shimmer_left` (an `AppTimer`
countdown set by `prv_refresh_to_exact`) keeps the minutes in snow for the first
few frames after a shake, then the frame that lands on zero renders clean.

## Time model

- `prv_display_hour()` — 12/24h from `clock_is_24h_style()` (system preference).
- `prv_floor5()` — minutes floored to a multiple of 5 for the passive display.
- `s_exact` — `false` after a scheduled passive repaint, `true` after a
  shake/tap/backlight-forced one. Drives the minute brightness and month colour.
- `s_drawn_hour` / `s_drawn_min` — what the last repaint actually put on screen,
  so a refresh that would change nothing skips the redraw entirely.

## Repaint schedule (`prv_tick_handler`)

The OS wakes the app every minute for its own clock. legere repaints only when
`tm_min % step == 0`, where `step` is **5** normally and **60** during Quiet
Time. `:00` and midnight are multiples of both, so hour and date rollover stay
covered. During Quiet Time the face can therefore be up to ~59 minutes stale —
accepted, because a deliberate look lights the backlight, which forces an exact
repaint (`prv_backlight_handler`).

Forced-exact paths:
- **`prv_backlight_handler`** — backlight on (button in the dark, flick-to-light).
  Stays live during Quiet Time, but skips if the passive hourly repaint already
  landed in this same minute (`s_sched_hour`/`s_sched_min`).
- **`prv_tap_handler`** — wrist flick/tap in daylight (when the backlight
  wouldn't fire). Suppressed entirely during Quiet Time (a sleeping wrist
  shouldn't relight the face).
- Both funnel through `prv_refresh_to_exact()`, which no-ops if the exact time is
  already on screen — this is what stops a walk from repainting every stride.

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
   (`s_digits_layer` / `s_date_layer` are disjoint; the date is repainted only on
   a rollover or an `s_exact` flip, shimmer ticks touch the digits layer only).
4. Cache the formatted time/date strings, re-render on change only — **done**
   (date strings keyed on `tm_mday` via `s_str_mday`; `hour_str`/`min_str` moved
   into the resource-failure fallback).

The 5-minute grid saves an estimated ~6–26 µA (≈ under one day over 21) — real
but a rounding error. It is kept as an identity choice, not a power play.

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

## Diagnostic log (temporary)

A `DayRecord` ring buffer (`DAYS_KEPT = 14`, 32 B/day in persist storage) records
per-hour shake-trigger counts and a Quiet Time bitmask. Two export paths:

- **AppMessage** → phone config page (`src/pkjs/index.js`), which builds a CSV
  and hands it to the Web Share API / a copy-paste textarea.
- **`APP_LOG` rows** in the exact CSV shape, one per finished hour, because the
  official Pebble app doesn't yet surface a Settings webview for sideloaded
  apps. `pebble logs | tee watch.log` then `tools/pebble-log-to-csv.py`.

Q17 (grill): a temporary hourly **battery-%** sample is to be added to
`DayRecord` while the face is being finished — to catch legere doing something
dumb, not to justify the grid. **All of this instrumentation comes out at store
launch.**

## Platforms

`emery` + `gabbro` only (both colour, 512 B glyph cache). The B&W code paths and
the small sprite sheet from the original design were dropped in `89c0224`.
