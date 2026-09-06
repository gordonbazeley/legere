# legere — Todo

Rough-priority order. Mark done or delete when shipped. Context for all of this
is the grill session captured in `decisions.md`.

## Face work

- [ ] **Minute-brightness freshness signal.** In `prv_canvas_update_proc`, tint
  the minute row `GColorLightGray` when `!s_exact`, `GColorWhite` when `s_exact`.
  Hour row stays `GColorDarkGray` unconditionally. Reuses `prv_set_ink`. Update
  the comment near `mon_color` — colour and brightness are now two signals.
- [ ] **Validate the overlap + dark-grey hour on hardware.** `67ae9f5` is
  unvalidated. Check dark-grey-on-black legibility unlit and the digit
  foot/head collision on `22:57` / `12:38` / `08:07`. If the hour is too dim,
  revert it to `GColorLightGray` and keep the −20 overlap.

## Power hygiene (the real lever — see `architecture.md` → Power model)

- [ ] **Digit block on its own layer.** Split the time digits out of
  `s_canvas_layer` into a child layer; `layer_mark_dirty` only that on a passive
  or forced repaint. The date row rarely changes — its own layer, marked dirty
  only on date rollover / `s_exact` flip.
- [ ] **Cache formatted strings.** Build `hour_str` / `min_str` / `dow` / `dom` /
  `mon` only when the underlying value changes, not every
  `prv_canvas_update_proc`. Keep `graphics_text_layout_get_content_size` off the
  redraw path (it already is — don't regress it).

## Locale (Latin-script scope — see `decisions.md`)

- [ ] `setlocale(LC_ALL, i18n_get_system_locale())` in `prv_init`.
- [ ] Expand the `FONT_DATE_*` `characterRegex` in `package.json` from
  `[A-Z0-9 ]` to cover Latin-1 accented capitals (À-Þ minus ×, plus any
  lowercase that survives if uppercasing misses them).
- [ ] Replace the ASCII `toupper` loops over `dow` / `mon` with something
  non-ASCII-safe, or drop the uppercasing for locales where it's wrong.
- [ ] Check the date row still fits the widest localised weekday/month at
  `FONT_DATE_21` on emery.

## Instrumentation (temporary — all of this comes out at store launch)

- [ ] **Add hourly battery-% to `DayRecord`.** One `uint8_t` (or min/max pair),
  sampled on the existing hourly path in `prv_tick_handler` via
  `battery_state_service_peek()`. Add it to the AppMessage export and the
  `APP_LOG` "row" line + `pebble-log-to-csv.py`. Purpose: spot legere doing
  something dumb on my own wrist. Not a grid A/B.
- [ ] **Removal checklist for store launch:** `DayRecord` + persist ring,
  `prv_log_trigger` / `prv_ensure_today` / `prv_persist_today` / `prv_day_key`,
  the whole `--- Phone export ---` block, `prv_outbox_*` / `prv_inbox_received`
  handlers, the `APP_LOG` "row" and "shake-wake" lines, `tools/pebble-log-to-csv.py`,
  the `Year`/`Mon`/`Mday`/`Shakes`/`QuietMask`/`Done`/`RequestLog` message keys.
  Decide then whether `src/pkjs/index.js` + the `configurable` capability stay
  for the ko-fi page or go with the export.

## Store v1

- [ ] **ko-fi link in the settings page.** Copy the approach used for the
  settings/companion pattern in `~/src/tidepebble` (`src/pkjs/settings.html` +
  the generated `settings-html.js` wrapper, `open_config.js` dev helper).
- [ ] **Listing pass.** Store assets (icon sizes, banner, screenshots),
  description copy. Pull the battery guidance from `architecture.md` → "Battery
  guidance (for the store listing)" into the description.
- [ ] Remove the diagnostic instrumentation (checklist above) before submitting.
- [ ] Confirm `author` / `displayName` / `uuid` in `package.json` are what you
  want on the store (`author` is currently `MakeAwesomeHappen`).

## Research — done

- [x] **HR 10 vs 30 min.** No published/measured figure exists — Core Devices
  publishes only the ordering (10min > 30min > hourly > off). Estimate: ~0.3–1
  mAh/day (~15–45 µA), ~1–4 fewer days on a 20–30 day baseline. PebbleOS skips
  HR when the watch is flat and never on the charger. Captured in
  `architecture.md` → Battery guidance.

## Housekeeping

- [ ] `README.md` still says "pebble-watchface" / generic boilerplate — rewrite
  it for legere (what it is, the shake mechanic, build steps).
- [ ] `design/plain.png` is a superseded mockup — replace with a current render
  or delete.
- [ ] Uncommitted as of this doc: the `icon.png` keyline change. Commit it.
