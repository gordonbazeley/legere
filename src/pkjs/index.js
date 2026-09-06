// Phone-side companion.
//
// showConfiguration opens the settings page (src/pkjs/settings.html, bundled as
// the generated string in settings-html.js): an info + support page with a
// ko-fi link, plus the "Time refresh" setting (real, see decisions.md) and a
// temporary diagnostic log.
//
// TEMPORARY: it also pulls the shake-to-wake log off the watch over AppMessage
// and injects it into the page's "Diagnostic log" section as a CSV + Share
// button. This whole block comes out with the rest of the instrumentation at
// store launch (src/dev_docs/todo.md); the watch also APP_LOGs the same rows
// for `pebble logs` + tools/pebble-log-to-csv.py.

var pad2 = function (n) { return (n < 10 ? '0' : '') + n; };

var days = [];         // DayRecords collected for this export
var redrawMode = 0;    // current watch setting, rides along with Done
var timeoutId = null;

function openPage() {
  clearTimeout(timeoutId);

  var rows = ['date,hour,quiet_hour,shakes,battery'];
  days.sort(function (a, b) {
    return (b.year - a.year) || (b.mon - a.mon) || (b.mday - a.mday);
  });
  days.forEach(function (d) {
    var date = pad2(d.mday) + '/' + pad2(d.mon) + '/' + d.year;
    for (var h = 23; h >= 0; h--) {
      var bat = d.battery[h];
      if (bat === 255) continue;  // hour hasn't happened yet (no battery sample) — no placeholder row
      var quiet = (d.quietMask & (1 << h)) ? 'yes' : 'no';
      rows.push(date + ',' + pad2(h) + ',' + quiet + ',' + d.shakes[h] + ',' + bat);
    }
  });

  var log = { days: days.length, csv: rows.join('\n'), redrawMode: redrawMode };
  // Escape anything that could break out of the injected <script> string.
  var init = 'var LOG=' + JSON.stringify(log).replace(/[<>&\u2028\u2029]/g, function (ch) {
    return '\\u' + ('0000' + ch.charCodeAt(0).toString(16)).slice(-4);
  }) + ';';

  // Function replacement so a "$" in the CSV isn't read as a special pattern.
  var html = require('./settings-html').replace('/*LOG_INIT*/', function () { return init; });
  Pebble.openURL('data:text/html;charset=utf-8,' + encodeURIComponent(html));
}

Pebble.addEventListener('showConfiguration', function () {
  days = [];
  Pebble.sendAppMessage({ 'RequestLog': 1 });
  // ponytail: fixed timeout instead of a real "give up" protocol — the watch
  // is a few AppMessages away over Bluetooth, 5s is generous. Bump if a fuller
  // DAYS_KEPT history ever makes the transfer slower.
  timeoutId = setTimeout(openPage, 5000);
});

Pebble.addEventListener('appmessage', function (e) {
  var d = e.payload;
  if (d.Done !== undefined) {
    if (d.RedrawMode !== undefined) redrawMode = d.RedrawMode;
    openPage();
    return;
  }
  if (d.Year !== undefined) {
    days.push({
      year: d.Year,
      mon: d.Mon,
      mday: d.Mday,
      shakes: d.Shakes,       // 24-byte array, one count per hour
      battery: d.Battery,     // 24-byte array, charge_percent per hour; 255 = no sample
      quietMask: d.QuietMask >>> 0
    });
  }
});

// Standard Pebble config-page handoff: settings.html's Save button navigates
// to pebblejs://close#<json>, which fires this event with that payload.
Pebble.addEventListener('webviewclosed', function (e) {
  if (!e.response) return;  // closed without saving (back button, etc.)
  var settings;
  try {
    settings = JSON.parse(decodeURIComponent(e.response));
  } catch (err) {
    return;
  }
  if (settings.redrawMode !== undefined) {
    redrawMode = settings.redrawMode;
    Pebble.sendAppMessage({ 'RedrawMode': redrawMode });
  }
});
