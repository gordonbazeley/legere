# pebble-watchface

A Pebble watchapp/watchface written in C using the Pebble SDK.

## Building & running

```sh
pebble build                          # build for all targetPlatforms (minified; copied to ~/Nextcloud/pbws)
pebble install --emulator emery       # install on the emery emulator
pebble install --phone <ip>           # install to a paired phone
```

## Target platforms

`targetPlatforms` in `package.json` controls which watches you build for. The
modern Pebble hardware is **emery** (Pebble Time 2), **gabbro** (Pebble Round
2), and **flint** (Pebble 2 Duo); the original Pebble platforms (aplite,
basalt, chalk, diorite) are included by default for backwards compatibility.

## Project layout

```
src/c/           C source for the watchapp
src/pkjs/        PebbleKit JS (phone-side) source, if any
worker_src/c/    Background worker source, if any
resources/       Images, fonts, and other bundled resources
package.json     Project metadata (UUID, platforms, resources, message keys)
wscript          Build rules — usually no need to edit
```

By default this project is configured as a watchapp. To make it a watchface,
set `pebble.watchapp.watchface` to `true` in `package.json`.

## Night colour

By day, minutes are the prominent digits (solid, larger visual weight) and
hours are secondary — the question during the day is "am I running late?",
which needs the exact minute.

At night, this flips: hours become the prominent (solid) digits and minutes
recede to a hollow outline. The question on waking at night is "should I go
back to sleep?", which only needs a rough sense of the hour, not the exact
minute.

## Documentation

Full SDK docs, tutorials, and API reference: <https://developer.repebble.com>
