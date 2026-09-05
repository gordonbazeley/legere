// Phone-side settings page: pulls the watch's shake-to-wake log over
// AppMessage and hands the user a CSV download. See src/c/legere.c
// for the on-watch side (DayRecord ring buffer + AppMessage export).

var pad2 = function(n) { return (n < 10 ? '0' : '') + n; };

var days = [];       // collected DayRecords for this export
var timeoutId = null;

function openReportPage() {
  clearTimeout(timeoutId);

  var rows = ['date,hour,quiet_hour,shakes'];
  days.sort(function(a, b) {
    return (a.year - b.year) || (a.mon - b.mon) || (a.mday - b.mday);
  });
  days.forEach(function(d) {
    var date = pad2(d.mday) + '/' + pad2(d.mon) + '/' + d.year;
    for (var h = 0; h < 24; h++) {
      var quiet = (d.quietMask & (1 << h)) ? 'yes' : 'no';
      rows.push(date + ',' + pad2(h) + ',' + quiet + ',' + d.shakes[h]);
    }
  });
  var csv = rows.join('\n');

  // ponytail: <a download> on a data: URI is a no-op in the Pebble app's
  // webview (WKWebView drops `download` for data: hrefs — tap does
  // literally nothing). Web Share API actually works there; a read-only
  // textarea is the fallback for anything that doesn't support sharing.
  var esc = function(s) { return s.replace(/[&<>]/g, function(c) {
    return { '&': '&amp;', '<': '&lt;', '>': '&gt;' }[c];
  }); };

  var html = '<!doctype html><meta charset="utf-8" name="viewport" content="width=device-width">' +
    '<title>Shake-to-wake log</title>' +
    '<body style="font-family:sans-serif;padding:20px;line-height:1.5">' +
    '<h3>Shake-to-wake log</h3>' +
    '<p>' + days.length + ' day(s) on the watch.</p>' +
    '<p><button id="share" style="font-size:16px;padding:8px 16px">Share CSV</button></p>' +
    '<p>If sharing doesn\'t pop up a menu, copy the text below instead:</p>' +
    '<textarea readonly style="width:100%;height:40vh;font-family:monospace" ' +
      'onclick="this.select()">' + esc(csv) + '</textarea>' +
    '<script>' +
      'var csv = ' + JSON.stringify(csv) + ';' +
      'document.getElementById("share").onclick = function() {' +
        'if (!navigator.share) { document.querySelector("textarea").select(); return; }' +
        'var data = { title: "shake-log.csv", text: csv };' +
        'try {' +
          'var file = new File([csv], "shake-log.csv", { type: "text/csv" });' +
          'if (navigator.canShare && navigator.canShare({ files: [file] })) data = { files: [file] };' +
        '} catch (e) {}' +
        'navigator.share(data).catch(function() {});' +
      '};' +
    '</script>' +
    '</body>';

  Pebble.openURL('data:text/html;charset=utf-8,' + encodeURIComponent(html));
}

Pebble.addEventListener('showConfiguration', function() {
  days = [];
  Pebble.sendAppMessage({ 'RequestLog': 1 });
  // ponytail: fixed timeout instead of a real "give up" protocol — the watch
  // is a few AppMessages away over Bluetooth, 5s is generous. Bump if a
  // fuller DAYS_KEPT history ever makes the transfer slower.
  timeoutId = setTimeout(openReportPage, 5000);
});

Pebble.addEventListener('appmessage', function(e) {
  var d = e.payload;
  if (d.Done !== undefined) {
    openReportPage();
    return;
  }
  if (d.Year !== undefined) {
    days.push({
      year: d.Year,
      mon: d.Mon,
      mday: d.Mday,
      shakes: d.Shakes,       // 24-byte array, one count per hour
      quietMask: d.QuietMask >>> 0
    });
  }
});
