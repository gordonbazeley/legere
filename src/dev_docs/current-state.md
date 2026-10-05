# legere — Current State

## What works

- `emery` and `gabbro` show stacked, pre-rendered hour/minute sprites.
- The OS tick repaints digits once a minute. Date text is cached; its row
  repaints on a date rollover or Night colour boundary.
- The phone settings page sends Night colour enabled/start/end in one payload,
  then re-sends saved values when the companion starts. The watch clamps and
  persists hours in the range 0–23.
- Night colour uses solid red hours and hollow red minutes. Day mode uses dark
  grey hours and white minutes.
- Date labels use the current system locale for the supported Latin-script
  locales. Emery selects a 24px or 20px date font based on measured width.
- `pebble build` regenerates the ignored settings wrapper, builds both targets,
  removes the source map, minifies phone JS, and copies the PBW to Nextcloud
  when available.

## Checks

- `npm ci && pebble build && node tools/check.js`
- `tools/check.js` verifies settings-hour clamping, startup re-sync, and
  generated UTF-8 output.
- CI pins its Actions, Pebble Tool 5.0.40, Pebble SDK 4.33.1, and Terser 5.51.2.
  It has read-only permissions except for the separate `pbw-latest` publisher.

## Known gaps

- Validate French and German date labels on real hardware.
- Validate dark-grey hour legibility and overlap on an unlit watch.
- Store listing text and day/night screenshots are in `store/`; re-shoot after any layout change. Validate gabbro date row (20px) with French/German labels.

## Key files

| File | Role |
|---|---|
| `src/c/legere.c` | Watchface rendering and persisted Night colour settings |
| `src/pkjs/index.js` | Phone-side settings handoff |
| `src/pkjs/settings.html` | Editable configuration page |
| `wscript` | Wrapper generation and PBW packaging |
| `tools/check.js` | Minimal settings and generated-wrapper regression check |
