# legere — Current State

## What works

- Stacked hour/minute digits on `emery` (200×228) and `gabbro` (180 round),
  pre-rendered from Alfa Slab One via `tools/gen-digits.sh`.
- Emery: digits 93×103, minute row overlapping the hour row −20px, hour row
  `GColorDarkGray`.
- Passive 5-minute repaint grid; hourly during Quiet Time.
- Wrist shake / tap (daylight) and backlight-on (dark) force an exact-minute
  repaint; `prv_refresh_to_exact` no-ops if the exact time is already shown.
- Quiet Time suppresses the tap path; the backlight path stays live but skips a
  repaint that already landed this minute.
- Freshness signal: month `GColorElectricBlue` when passive, `GColorRed` when
  exact.
- 12/24h from the system (`clock_is_24h_style`).
- Repaint skipped whenever it would not change the screen (`s_drawn_hour` /
  `s_drawn_min`).
- Diagnostic log: `DayRecord` ring (14 days), per-hour shake counts + Quiet Time
  bitmask, in persist storage.
- Log export: AppMessage → `src/pkjs/index.js` → CSV via Web Share / textarea;
  and `APP_LOG` rows for `pebble logs` capture + `tools/pebble-log-to-csv.py`.
- Launcher icon (`resources/images/icon.png`) reads on any launcher background
  (white glyph + black keyline).
- Builds via `pebble build`; `tools/strip-js-sourcemap.sh` drops the unused
  ~5.7 KB JS source map from the `.pbw`.

## Not done yet — see `todo.md`

- **Minute-brightness freshness signal.** Design agreed (LightGray passive →
  White exact) but not implemented — minutes are currently always `GColorWhite`.
- **Power-hygiene pass.** The digit block is not on its own layer; the whole
  `s_canvas_layer` (full bounds) is marked dirty every repaint. Formatted
  time/date strings are rebuilt every `prv_canvas_update_proc` call.
- **Locale.** No `setlocale()` call — `strftime` runs in the C locale, always
  English. Date font subset is `[A-Z0-9 ]`; `toupper` is ASCII-only. "Locale
  from system settings" is a stated goal, not wired.
- **Temporary battery-% sampling** in `DayRecord` (Q17) — not added.
- **ko-fi link** in the settings page — not added (copy the approach from
  `~/src/tidepebble`).
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
- **`configurable` capability + AppMessage keys are load-bearing only for the
  diagnostic export.** When the export is removed at store launch, decide
  whether the companion stays (for the ko-fi page) or goes entirely.
- **`prv_measure` uses a fixed 400×300 layout box.** Fine for the short strings
  used, but it is a text-layout call — keep it out of any redraw path (currently
  only `prv_window_load` calls it).
- **`s_date_font` fallback.** If the custom font fails to load, falls back to
  `FONT_KEY_GOTHIC_14_BOLD` and `s_date_font_custom` guards the unload. The
  digit sheet has no equivalent fallback — `prv_canvas_update_proc` draws plain
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
- Some `pebble` commands need the sandbox disabled in this environment (they
  write into the repo tree).

## File inventory

| File | Role |
|---|---|
| `src/c/legere.c` | Entire watch app (~467 lines) |
| `src/pkjs/index.js` | Phone companion: requests the log, builds CSV, Web Share / textarea (~85 lines) |
| `package.json` | Pebble metadata, message keys, resources |
| `wscript` | SDK build rules (unmodified) |
| `resources/fonts/AlfaSlabOne-Regular.ttf` | Source for the digit sprite sheets |
| `resources/fonts/MichromaText.ttf` | Date row font (loaded as `FONT_DATE_14` / `FONT_DATE_21`) |
| `resources/images/digits.png` | gabbro sprite sheet — slot 58×62 |
| `resources/images/digits_lg.png` | emery sprite sheet — slot 93×103 |
| `resources/images/icon.png` | Launcher icon, 25×25, white glyph + black keyline |
| `tools/gen-digits.sh` | Regenerates the sprite sheets from the TTF |
| `tools/punch-holes.py` | Grows enclosed counters without touching the outer silhouette |
| `tools/quantize-alpha.py` | Quantises glyph-edge alpha to N even steps |
| `tools/pebble-log-to-csv.py` | Pulls CSV rows out of a `pebble logs` capture |
| `tools/strip-js-sourcemap.sh` | Drops `pebble-js-app.js.map` from the `.pbw` |
| `design/plain.png` | Old single-line mockup — superseded, kept for reference |
