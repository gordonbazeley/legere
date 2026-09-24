#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
pebble build
zip -d build/legere.pbw 'pebble-js-app.js.map'
npx terser build/pebble-js-app.js -c -m -o build/pebble-js-app.js
zip -j build/legere.pbw build/pebble-js-app.js
cp build/legere.pbw ~/Nextcloud/pbws/
echo "Store build: $(wc -c < build/legere.pbw | tr -d ' ') bytes -> build/legere.pbw (copied to ~/Nextcloud/pbws)"
