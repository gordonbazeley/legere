# Legere

Pebble watchface (C + PebbleKit JS), targets emery and gabbro.

## Build

- `pebble build` — minifies pkjs and copies the .pbw to `~/Nextcloud/pbws` (done by `wscript`, no manual copy).
- `pebble install --emulator emery` to test.

## Branches

Active line is `origin/main`. Run `git fetch` before assuming local state is current; `shake-wake-log` is a stale ancestor.

## Docs (what `dcp` keeps current)

- `README.md`
- `CHANGELOG.md` — add entries under "Unreleased"
- `src/dev_docs/` — `current-state.md`, `decisions.md`, `todo.md`, `architecture.md`; record design decisions with a dated heading in `decisions.md`
