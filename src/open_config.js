#!/usr/bin/env node
// Dev-only: preview src/pkjs/settings.html in a browser. The real config flow
// hands the page to the Pebble app as a data: URI (see pkjs/index.js), which
// is awkward to iterate on. This just serves the file over http://127.0.0.1
// and opens it in the default browser.
//
//   node src/open_config.js           # ko-fi page only
//   node src/open_config.js --log     # also fill the temporary diagnostic-log section with sample rows

var http = require('http');
var fs = require('fs');
var path = require('path');
var child_process = require('child_process');

var SETTINGS_HTML = path.resolve(__dirname, 'pkjs', 'settings.html');
if (!fs.existsSync(SETTINGS_HTML)) {
  console.error('Not found:', SETTINGS_HTML);
  process.exit(1);
}

var withLog = process.argv.indexOf('--log') !== -1;

function sampleLog() {
  var rows = ['date,hour,quiet_hour,shakes'];
  for (var h = 0; h < 24; h++) {
    var quiet = (h < 7 || h >= 23) ? 'yes' : 'no';
    rows.push('05/09/2026,' + (h < 10 ? '0' : '') + h + ',' + quiet + ',' + (h % 4));
  }
  return 'var LOG={days:1,csv:' + JSON.stringify(rows.join('\n')) + '};';
}

var server = http.createServer(function(req, res) {
  var html = fs.readFileSync(SETTINGS_HTML, 'utf8'); // re-read each request so edits show on refresh
  if (withLog) html = html.replace('/*LOG_INIT*/', sampleLog());
  res.writeHead(200, { 'Content-Type': 'text/html; charset=utf-8' });
  res.end(html);
});

server.listen(0, '127.0.0.1', function() {
  var url = 'http://127.0.0.1:' + server.address().port + '/';
  console.log('Serving settings.html at ' + url + ' — Ctrl-C to stop.');
  try { child_process.execFileSync('open', [url]); } catch (e) { /* open the URL yourself */ }
});
