#!/bin/sh
# ponytail: pebble-tool always emits pebble-js-app.js.map when enableMultiJS
# is on (baked into the vendored waf script — no webpack.config.js hook
# exists to turn it off). The map only helps phone-side JS debugging; the
# app runs fine without it. Run this after `pebble build` to drop it from
# the .pbw, saving ~5.7KB.
set -eu
cd "$(dirname "$0")/.."
zip -d build/legere.pbw pebble-js-app.js.map
