# Legere — Todo

Rough-priority order. Mark done or delete when shipped. Context for all of this
is the grill session captured in `decisions.md`.

## Face work

- [x] **Passive-minute freshness signal.** Done — TV static via `prv_staticify`
  (framebuffer snow), plus a lock-on ramp on shake
  (`s_shimmer_left` / `prv_shimmer_tick`). See `decisions.md`.
- [x] **Lock-on ramp instead of snow-then-clean.** Done — `prv_snow_permille()`
  steps the snowed fraction of the minute ink `PASSIVE_SNOW_PERMILLE`→0 (that
  ceiling has since been tuned down from a full 1000 to 350 — see
  `decisions.md`) over `SHIMMER_FRAMES`, so the digits surface out of the
  noise. `prv_staticify` gained a `permille` arg.
- [x] **Symmetric ramp on loss of lock.** Done — `s_shimmer_out` runs the same
  ramp in reverse (0→1000) when the clock ticks past the locked minute;
  `prv_shimmer_tick` clears `s_exact` on the last frame. Was a one-frame hard cut.
- [ ] **Tune the lock-on ramp on hardware.** `SHIMMER_FRAMES 8` / `SHIMMER_MS 40`
  (~320 ms) is a guess. Check the feel on a real wrist — frame count, step
  time, whether a linear permille ramp reads right or wants an ease. Applies to
  both directions (lock-on and lock-out share the constants).
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
- [x] Expand the `FONT_DATE_*` `characterRegex`. Done — swapped the font file
  from `MichromaText.ttf` (a hand-subset copy with only `[A-Z0-9 ]` — no
  accents, no `.`) to the full `Michroma-Regular.ttf`, with `characterRegex`
  doing the subsetting at build. The date font itself later changed twice more
  (Orbitron Bold, then Quantico Bold — see `decisions.md`), each time keeping
  a regex covering the same accented-capitals need; current value is
  `[A-Z0-9 .À-Öß]`.
- [x] Replace the ASCII `toupper` loops. Done — `prv_utf8_upper` handles ASCII +
  the whole Latin-1 accented lowercase block (0xC3 0xA0..0xBE). ß has no
  single-char uppercase, left as-is.
- [x] Check the date row fits. Done — the widest realistic row is French
  "SEPT. 06 AOÛT" ("sept." abbreviation + accented weekday), which collides at
  the font's large size. Rather than hardcode a locale list, `prv_pick_date_font`
  measures every weekday + month abbreviation the active locale produces at
  load and uses the large size if they fit, else the small one. Sizes have
  since moved with the font/size tuning (was 21/18px, now 24/20px — see
  `decisions.md`); the fit-or-fall-back logic is unchanged. Verified in the
  emulator: EN picks the large size, forced-wide strings pick the small one
  and clear.
- [ ] **Validate locale on hardware.** Emulator can't install a language pack,
  so the real localised strings are still untested. Install FR (worst case) + DE
  packs on emery, confirm the row fits unlit and the accented caps render.

## Instrumentation — removed 2026-09-07

- [x] **All diagnostic instrumentation removed.** `DayRecord` + persist ring,
  `prv_log_trigger` / `prv_ensure_today` / `prv_persist_today` / `prv_day_key`,
  the `--- Phone export ---` block + `prv_outbox_*` handlers, the per-minute
  battery/Quiet-Time sampler, the `APP_LOG` "row"/"shake-wake" lines,
  `tools/pebble-log-to-csv.py`, seven of the eight message keys, the log-fetch
  code in `index.js`, and the `#logSection` + `--log` branch in `settings.html`
  / `open_config.js`. The audit found the per-minute `persist_write_data` was
  the only meaningful battery/flash cost in the codebase and the grid question
  it answered is settled. **Kept:** the night-colour setting + settings page
  (`prv_inbox_received_handler` trimmed to just that branch;
  `app_message_open(..., 0)`, inbox-only), the `configurable` capability. Old
  persist keys (190, 200–213) on installed watches left to rot — no migration.
  (Shake-to-wake and the `RedrawMode` setting mentioned here at the time were
  themselves removed 2026-09-28 — see `decisions.md`.)

## Store v1

- [x] **ko-fi link in the settings page.** Done — `src/pkjs/settings.html`
  is the editable source; `wscript` generates the ignored runtime wrapper.
  The page has Night colour, GitHub issues, and the ko-fi button.
- [ ] **Listing pass.** Store assets (icon sizes, banner, screenshots),
  description copy. Pull the battery guidance from `architecture.md` → "Battery
  guidance (for the store listing)" into the description.
- [x] `author` in `package.json` set to `ModusApps` (matches `~/src/tidepebble`).
  Still confirm `displayName` (`Legere`) / `uuid` before store submit.

## Research — done

- [x] **HR 10 vs 30 min.** No published/measured figure exists — Core Devices
  publishes only the ordering (10min > 30min > hourly > off). Estimate: ~0.3–1
  mAh/day (~15–45 µA), ~1–4 fewer days on a 20–30 day baseline. PebbleOS skips
  HR when the watch is flat and never on the charger. Captured in
  `architecture.md` → Battery guidance.

## Housekeeping

- [ ] `README.md` still says "pebble-watchface" / generic boilerplate — rewrite
  it for Legere (what it is, the shake mechanic, build steps).
- [x] `design/plain.png` superseded mockup — deleted.
- [x] `icon.png` keyline change — committed (`4cb6c23`).
- [x] `resources/fonts/MichromaText.ttf` unused since the locale work — deleted.
