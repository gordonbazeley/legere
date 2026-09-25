// Phone-side companion.
//
// showConfiguration opens the settings page (src/pkjs/settings.html, bundled as
// the generated string in settings-html.js): an info + support page with a
// ko-fi link, plus the "Time refresh" and "Night colour" settings (real, see
// decisions.md).
//
// Both are relayed to the watch as AppMessages (MESSAGE_KEY_RedrawMode,
// MESSAGE_KEY_NightEnabled/NightStart/NightEnd), which the watch persists and
// reads at boot. The phone keeps the last saved values in localStorage only
// to pre-fill the settings page next time it opens.

function currentRedrawMode() {
  try {
    return localStorage.getItem('redrawMode') === '1' ? 1 : 0;
  } catch (e) {
    return 0;
  }
}

function currentNight() {
  try {
    return {
      enabled: localStorage.getItem('nightEnabled') === '1',
      start: Number(localStorage.getItem('nightStart') || 21),
      end: Number(localStorage.getItem('nightEnd') || 7),
    };
  } catch (e) {
    return { enabled: false, start: 21, end: 7 };
  }
}

Pebble.addEventListener('showConfiguration', function () {
  var init = 'var REDRAW_MODE=' + currentRedrawMode() + ';' +
    'var NIGHT=' + JSON.stringify(currentNight()) + ';';
  var html = require('./settings-html').replace('/*INIT*/', init);
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
  if (settings.nightEnabled !== undefined) {
    try {
      localStorage.setItem('nightEnabled', String(settings.nightEnabled));
      localStorage.setItem('nightStart', String(settings.nightStart));
      localStorage.setItem('nightEnd', String(settings.nightEnd));
    } catch (e) {}
    Pebble.sendAppMessage({
      'NightEnabled': settings.nightEnabled,
      'NightStart': settings.nightStart,
      'NightEnd': settings.nightEnd,
    });
  }
});
