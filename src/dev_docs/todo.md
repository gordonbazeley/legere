# legere — Todo

Rough-priority order. Mark done or delete when shipped. Context for all of this
is the grill session captured in `decisions.md`.

## Face work

- [x] **Passive-minute freshness signal.** Done — TV static via `prv_staticify`
  (framebuffer snow), plus a ~275 ms lock-on flicker on shake
  (`s_shimmer_left` / `prv_shimmer_tick`). See `decisions.md`.
- [ ] **Tune the shimmer on hardware.** `SHIMMER_FRAMES 5` / `SHIMMER_MS 55` is a
  guess. Check the lock-on feel on a real wrist — may want longer/shorter, or a
  brightness ramp rather than pure snow-then-clean.
- [x] **Check the static on gabbro** (round, 58×62 slot). Done — verified in the
  gabbro emulator (`22:57` passive + shake-to-exact). Snow renders correctly at
  the 58×62 slot on gabbro's round 8-bit framebuffer: clean glyph outline, only
  the minute row snowed, hour stays solid dark grey, nothing bleeds into the
  date row. `prv_staticify`'s per-row `gbitmap_get_data_row_info` clamping is
  what makes it round-safe.
- [x] **Retune the gabbro layout for 260×260.** Done — `digits` sheet regenerated
  at `88 80` (slot 73×80, was 58×62); round constants moved off the 180 values:
  `PAD 20`, `DIGIT_GAP 6`, `DIGIT_BAND_BOT_GAP 6`, `top_margin 28`,
  `bot_margin 32`, `grid_w` now `= s_usable_w`, round `date_w 174`. Digits fill
  ~56% width, dead strip gone, block vertically balanced. Verified in the gabbro
  emulator (passive static, shake-to-exact, date clears the arc). Emery
  unchanged. Still worth a hardware glance for arc clearance on the date row and
  digit legibility unlit.
- [x] **Validate the overlap + dark-grey hour on hardware.** `67ae9f5` is
  unvalidated. Check dark-grey-on-black legibility unlit and the digit
  foot/head collision on `22:57` / `12:38` / `08:07`. If the hour is too dim,
  revert it to `GColorLightGray` and keep the −20 overlap.

## Power hygiene (the real lever — see `architecture.md` → Power model)

- [x] **Digit block on its own layer.** Done — `s_digits_layer` (0..`s_date_top`)
  and `s_date_layer` (`s_date_top`..) are disjoint, so marking one never re-runs
  the other's update proc. Shimmer ticks mark digits only; the passive grid tick
  marks the date only on a rollover / `s_exact` flip; a forced-exact refresh
  marks both.
- [x] **Cache formatted strings.** Done — the date strings (`s_dow` / `s_dom` /
  `s_mon`) rebuild only when `tm_mday` changes (`s_str_mday`). `hour_str` /
  `min_str` are now built only in the resource-failure fallback branch (the
  normal path uses the `dv[]` int digits). `graphics_text_layout_get_content_size`
  stays in `prv_window_load` only.
- [ ] **(maybe) Hour digits on their own layer.** Split the hour row out of
  `s_digits_layer` so a minute-only repaint (every passive grid tick except
  `:00`, every shimmer frame, every forced-exact refresh) skips the 2 hour-digit
  blits. Estimated saving ~10 nA average (≈ 1 s of battery life over 21 days) —
  below noise, so this is a "only if the render profile ever matters" note, not a
  real todo. Caveats: on emery `DIGIT_GAP = -20` overlaps the rows, so the minute
  dirty region drags the hour layer's update proc back in for the overlap strip;
  and `prv_staticify`'s `min_rect` coords get more fragile. Needs an hour-digit /
  hour-string cache and `:00` dirty-tracking.

## Locale (Latin-script scope — see `decisions.md`)

- [x] `setlocale(LC_ALL, i18n_get_system_locale())` in `prv_init`. Done.
- [x] Expand the `FONT_DATE_*` `characterRegex`. Done — now
  `[A-Z0-9 .À-ÖØ-Þß]`. Also swapped the font file from `MichromaText.ttf` (a
  hand-subset copy with only `[A-Z0-9 ]` — no accents, no `.`) to the full
  `Michroma-Regular.ttf`; `characterRegex` does the subsetting at build.
- [x] Replace the ASCII `toupper` loops. Done — `prv_utf8_upper` handles ASCII +
  the whole Latin-1 accented lowercase block (0xC3 0xA0..0xBE). ß has no
  single-char uppercase, left as-is.
- [x] Check the date row fits. Done — the widest realistic row is French
  "SEPT. 06 AOÛT" ("sept." abbreviation + accented weekday), which collides at
  21px. Rather than hardcode a locale list, `prv_pick_date_font` measures every
  weekday + month abbreviation the active locale produces at load and uses 21px
  if they fit, else 18px. EN/DE/IT/PT/NL keep 21; FR (and probably ES) get 18.
  Verified in the emulator: EN picks 21, forced-wide strings pick 18 and clear.
- [ ] **Validate locale on hardware.** Emulator can't install a language pack,
  so the real localised strings are still untested. Install FR (worst case) + DE
  packs on emery, confirm the row fits unlit and the accented caps render.

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
- [ ] `resources/fonts/MichromaText.ttf` is now unused (locale work switched the
  date row to `Michroma-Regular.ttf`). Delete it.
