// Phone-side companion.
//
// showConfiguration opens the settings page (src/pkjs/settings.html, bundled as
// the generated string in settings-html.js): an info + support page with a
// ko-fi link, plus the "Night colour" setting (real, see decisions.md).
//
// It's relayed to the watch as AppMessages (MESSAGE_KEY_NightEnabled/
// NightStart/NightEnd), which the watch persists and reads at boot. The phone
// keeps the last saved values in localStorage only to pre-fill the settings
// page next time it opens.

function clampHour(n, fallback) {
  n = Number(n);
  if (isNaN(n)) return fallback;
  return Math.min(23, Math.max(0, Math.round(n)));
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

// AppMessages can be NACKed (watch busy, e.g. right after the config webview
// closes or while the face is still starting) and are otherwise silently
// dropped, so retry a few times before giving up.
function sendWithRetry(message, triesLeft) {
  Pebble.sendAppMessage(message, function () {
    console.log('Night colour sent: ' + JSON.stringify(message));
  }, function (e) {
    console.log('Night colour send failed (' + triesLeft + ' tries left)');
    if (triesLeft > 0) {
      setTimeout(function () { sendWithRetry(message, triesLeft - 1); }, 1000);
    }
  });
}

function syncNight() {
  var night = currentNight();
  if (night.enabled || localStorage.getItem('nightEnabled') !== null) {
    sendWithRetry({
      'NightEnabled': night.enabled ? 1 : 0,
      'NightStart': clampHour(night.start, 21),
      'NightEnd': clampHour(night.end, 7),
    }, 3);
  }
}

Pebble.addEventListener('ready', syncNight);

Pebble.addEventListener('showConfiguration', function () {
  var init = 'var NIGHT=' + JSON.stringify(currentNight()) + ';';
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
  if (settings.nightEnabled !== undefined) {
    var enabled = settings.nightEnabled ? 1 : 0;
    var start = clampHour(settings.nightStart, 21);
    var end = clampHour(settings.nightEnd, 7);
    try {
      localStorage.setItem('nightEnabled', String(enabled));
      localStorage.setItem('nightStart', String(start));
      localStorage.setItem('nightEnd', String(end));
    } catch (e) {}
    syncNight();
  }
});
