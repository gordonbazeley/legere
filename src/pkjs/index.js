// Phone-side companion.
//
// showConfiguration opens the settings page (src/pkjs/settings.html, bundled as
// the generated string in settings-html.js): an info + support page with a
// ko-fi link, plus the "Time refresh" setting (real, see decisions.md).
//
// "Time refresh" is relayed to the watch as MESSAGE_KEY_RedrawMode, which the
// watch persists and reads at boot. The phone keeps the last saved value in
// localStorage only to pre-select the radio next time the page opens.

function currentRedrawMode() {
  try {
    return localStorage.getItem('redrawMode') === '1' ? 1 : 0;
  } catch (e) {
    return 0;
  }
}

Pebble.addEventListener('showConfiguration', function () {
  var html = require('./settings-html').replace(
    '/*INIT*/', 'var REDRAW_MODE=' + currentRedrawMode() + ';');
  Pebble.openURL('data:text/html;charset=utf-8,' + encodeURIComponent(html));
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
    try { localStorage.setItem('redrawMode', String(settings.redrawMode)); } catch (e) {}
    Pebble.sendAppMessage({ 'RedrawMode': settings.redrawMode });
  }
});
