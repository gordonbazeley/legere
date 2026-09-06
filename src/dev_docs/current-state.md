# legere — Current State

## What works

- Stacked hour/minute digits on `emery` (200×228) and `gabbro` (260×260 round),
  pre-rendered from Alfa Slab One via `tools/gen-digits.sh`.
- Emery: digits 93×103, minute row overlapping the hour row −20px, hour row
  `GColorDarkGray`.
- Gabbro: digits 73×80, rows a positive 6px apart, centred; hour row
  `GColorDarkGray`.
- Minute row is offset `MINUTE_ROW_SHIFT_X` (15px) left of the hour row's
  centred position, on both platforms — a slight horizontal stagger.
- Passive 5-minute repaint grid, Quiet Time or not (no hourly fallback) — or
  a plain every-minute mode with no static/shake at all, if the user has
  picked that in the "Time refresh" setting (`s_every_minute`,
  `MESSAGE_KEY_RedrawMode`, persisted). Stopgap until touch is enabled for
  watchapps; see `decisions.md`.
- Wrist shake / tap (daylight) and backlight-on (dark) force an exact-minute
  repaint; `prv_refresh_to_exact` no-ops if the exact time is already shown.
- Quiet Time suppresses the tap path; the backlight path stays live but skips a
  repaint that already landed this minute.
- `s_passive_min` holds the passive minute as state rather than recomputing a
  floor from live time on every redraw — kept grid-aligned during normal
  operation, but pinned to the real minute an expiring exact reading just
  showed until the next scheduled tick, so the display never jumps backward
  to the grid mark *before* that minute (exact "12:33" expiring no longer
  shows static "12:30" — it holds "12:34" until the next 5-minute mark).
- Freshness signal is **the minute static alone**: minute digits rendered as TV
  static when passive, resolving to solid white on a shake via a ~320 ms lock-on
  ramp — `prv_snow_permille()` steps the snowed fraction of the minute ink
  `PASSIVE_SNOW_PERMILLE`→0 (350, dialed back from a full 1000) over
  `SHIMMER_FRAMES` (`prv_staticify` + `s_shimmer_left` / `prv_shimmer_tick`).
  The ramp is symmetric: when the clock ticks past the locked minute it plays
  in reverse (`s_shimmer_out`), so the minute decays back into static rather
  than cutting out in one frame. The whole date row is a constant
  `GColorWhite` (`DATE_COLOR`) — no red/blue freshness cue there any more.
- 12/24h from the system (`clock_is_24h_style`).
- Repaint skipped whenever it would not change the screen (`s_drawn_hour` /
  `s_drawn_min`).
- Digits and date on disjoint layers (`s_digits_layer` / `s_date_layer`); each
  marked dirty only when its own content changes. Date strings cached, rebuilt
  on `tm_mday` change only.
- Diagnostic log: `DayRecord` ring (14 days), per-hour shake counts + Quiet Time
  bitmask + battery `charge_percent` (`0xFF` = no sample), in persist storage.
  `battery[24]` is last in the struct so pre-battery 32-byte blobs still read
  back cleanly. CSV gains a `battery` column (integer percent).
- Log export, two paths (both temporary, out at store launch), both hour-by-
  hour, both newest-first — neither ever writes a row for an hour that hasn't
  happened yet:
  - **`APP_LOG` rows** in the exact CSV shape, one per *finished* hour —
    `pebble logs` capture + `tools/pebble-log-to-csv.py` (buffers the matched
    rows and prints newest-first; the raw log capture is oldest-first). The
    in-progress hour is never logged; earlier hours only appear if capture was
    running at each hour boundary.
  - **AppMessage** → `src/pkjs/index.js` → the settings page's "Diagnostic
    log" section (CSV + Web Share / textarea). Sends every persisted
    `DayRecord` slot including today's partial one, sorted newest-day-first
    with each day's hours 23→0, and `openPage()` skips any hour whose
    `battery` sample is still `255` before building the CSV —
    so today's still-to-come hours never show up as placeholder rows.
- Companion settings page (`src/pkjs/settings.html`, generated string in
  `settings-html.js`): "Time refresh" (the setting above — pre-selects from
  the watch's current value, Save closes the page via the standard
  `pebblejs://close#<json>` handoff, `index.js`'s `webviewclosed` listener
  relays it back as `RedrawMode`), then a temporary diagnostic-log section,
  then info + GitHub issues + ko-fi link. Preview with `node src/open_config.js`
  (`--log` for sample rows, `--redraw=1` to preview the radio pre-selected).
- Launcher icon (`resources/images/icon.png`) reads on any launcher background
  (white glyph + black keyline).
- Builds via `pebble build`; `tools/strip-js-sourcemap.sh` drops the unused
  ~5.7 KB JS source map from the `.pbw`.

## Not done yet — see `todo.md`

- **Locale — mostly wired (Latin-script scope).** `prv_init` calls
  `setlocale(LC_ALL, i18n_get_system_locale())`; `strftime %a/%b` then follow the
  installed language pack (English if none). `prv_utf8_upper` uppercases ASCII +
  the Latin-1 accented block. Date font is `Quantico-Bold.ttf` subset to
  `[A-Z0-9 .À-Öß]` (Quantico's cmap has no gaps in that range). Emery date
  font is picked at window load
  (`prv_pick_date_font`): 24px normally, 20px only if the locale's widest
  weekday+month abbreviations wouldn't fit at 24 (FR/ES, with "SEPT." + accented
  period-weekdays). Untested on real hardware with a non-English language pack.
- **Store listing pass** — no store assets, description, or screenshots; the
  diagnostic instrumentation is still in.
- **Hardware validation** of the `67ae9f5` overlap + dark-grey hour, especially
  dark-grey-on-black legibility on the unlit transflective LCD.

## Known gaps / risk

- **Dark-grey hour unlit.** Pebble has no mid-grey (channels quantise to
  0/85/170/255), so the hour row is `GColorDarkGray` (0x555555). On a black
  background on the unlit LCD this may be hard to read. Fallback: revert the
  hour row to `GColorLightGray`, keep the overlap. Needs an on-wrist check.
- **Overlap digit collision.** ~30–40% of times have the hour digit's foot
  overpainted by the minute digit's head (e.g. `22:57`). Intentional; grey/white
  contrast carries it, but it's the main thing to sanity-check on hardware.
- **`configurable` capability stays** — it backs the settings/ko-fi page. The
  diagnostic-log AppMessage export (`Year`/`Mon`/`Mday`/`Shakes`/`Battery`/
  `QuietMask`/`RequestLog`/`Done` message keys, `prv_outbox_*` /
  `prv_inbox_received`) comes out at store launch, leaving `index.js` with
  just the `showConfiguration` opener and the `RedrawMode` setting relay.
  `RedrawMode` is a *separate*, not-temporary message key — it stays until
  touch is enabled for watchapps (see `decisions.md`), independent of the
  diagnostic-instrumentation removal.
- **`prv_measure` uses a fixed 400×300 layout box.** Fine for the short strings
  used, but it is a text-layout call — keep it out of any redraw path (currently
  only `prv_window_load` calls it).
- **`s_date_font` fallback.** If the custom font fails to load, falls back to
  `FONT_KEY_GOTHIC_14_BOLD` and `s_date_font_custom` guards the unload. The
  digit sheet has no equivalent fallback — `prv_digits_update_proc` draws plain
  `FONT_KEY_LECO_42_NUMBERS` text if `s_sheet` is NULL.
- No automated tests.

## Build / toolchain

- Pebble SDK 3 declared; `enableMultiJS: true`.
- `pebble build` → `build/legere.pbw`.
- Regenerate sprite sheets: `bash tools/gen-digits.sh` (needs ImageMagick +
  Python 3 + Pillow). Prints the resulting `slot WxH` — re-check the emery
  vertical/width budget in `architecture.md` after any change.
- Emulator: `pebble install --emulator emery` (or `gabbro`). `pebble emu-set-time`
  did not reliably stick in testing — the emulator tends to track host time.
- CI (`.github/workflows/build-pbw.yml`): every push to `main` installs
  pebble-tool + the SDK (`pebble sdk install latest`, cached on
  `~/.pebble-sdk`) and runs `pebble build`. Uploads the `.pbw` as a normal
  Actions artifact, and — since that artifact storage isn't reachable from
  every environment that might want the build — also force-pushes it as the
  sole file on an orphan `pbw-latest` branch, fetchable with nothing but
  `git fetch origin pbw-latest && git show origin/pbw-latest:legere.pbw > legere.pbw`.
- Some `pebble` commands need the sandbox disabled in this environment (they
  write into the repo tree).

## File inventory

| File | Role |
|---|---|
| `src/c/legere.c` | Entire watch app (~700 lines) |
| `CHANGELOG.md` | User-facing changelog — `## Unreleased` plus dated sections |
| `.github/workflows/build-pbw.yml` | CI: builds the `.pbw` on every push to `main`, publishes it to Actions artifacts and the `pbw-latest` branch |
| `src/pkjs/index.js` | Phone companion: pulls the log over AppMessage (temporary) + opens the settings page on `showConfiguration` |
| `src/pkjs/settings.html` | Companion settings page — Time refresh setting, then a temporary diagnostic-log section, then info + GitHub issues + ko-fi link (editable source) |
| `src/pkjs/settings-html.js` | Generated CommonJS string of `settings.html`, loaded by pkjs — regen after editing the HTML |
| `src/open_config.js` | Dev helper: serves `settings.html` on localhost for browser preview (`--log` injects sample rows) |
| `package.json` | Pebble metadata, message keys, resources |
| `wscript` | SDK build rules (unmodified) |
| `resources/fonts/AlfaSlabOne-Regular.ttf` | Source for the digit sprite sheets |
| `resources/fonts/Quantico-Bold.ttf` | Date row font (`FONT_DATE_16` on gabbro; `FONT_DATE_24` or `_20` on emery, picked at load) |
| `resources/images/digits.png` | gabbro sprite sheet — slot 73×80 |
| `resources/images/digits_lg.png` | emery sprite sheet — slot 93×103 |
| `resources/images/icon.png` | Launcher icon, 25×25, white glyph + black keyline |
| `tools/gen-digits.sh` | Regenerates the sprite sheets from the TTF |
| `tools/punch-holes.py` | Grows enclosed counters without touching the outer silhouette |
| `tools/quantize-alpha.py` | Quantises glyph-edge alpha to N even steps |
| `tools/pebble-log-to-csv.py` | Pulls CSV rows out of a `pebble logs` capture |
| `tools/strip-js-sourcemap.sh` | Drops `pebble-js-app.js.map` from the `.pbw` |
