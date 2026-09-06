// Phone-side companion. legere has no configurable settings — the config
// button opens an info + support page (src/pkjs/settings.html, bundled as the
// generated string in settings-html.js).
//
// The shake-to-wake log export lives outside this file now: the watch APP_LOGs
// one CSV row per finished hour, pulled out with `pebble logs` +
// tools/pebble-log-to-csv.py. See src/c/legere.c.

Pebble.addEventListener('showConfiguration', function() {
  Pebble.openURL('data:text/html;charset=utf-8,' +
    encodeURIComponent(require('./settings-html')));
});
