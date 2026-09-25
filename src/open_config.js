#!/usr/bin/env node
// Dev-only: preview src/pkjs/settings.html in a browser. The real config flow
// hands the page to the Pebble app as a data: URI (see pkjs/index.js), which
// is awkward to iterate on. This just serves the file over http://127.0.0.1
// and opens it in the default browser.
//
//   node src/open_config.js               # radio pre-selected to "every 5 minutes"
//   node src/open_config.js --redraw=1    # radio pre-selected to "every minute"
//   node src/open_config.js --night=1     # night colour checkbox pre-checked

var http = require('http');
var fs = require('fs');
var path = require('path');
var child_process = require('child_process');

var SETTINGS_HTML = path.resolve(__dirname, 'pkjs', 'settings.html');
if (!fs.existsSync(SETTINGS_HTML)) {
  console.error('Not found:', SETTINGS_HTML);
  process.exit(1);
}

var redrawArg = process.argv.filter(function (a) { return a.indexOf('--redraw=') === 0; })[0];
var redrawMode = redrawArg ? Number(redrawArg.split('=')[1]) : 0;
var nightArg = process.argv.filter(function (a) { return a.indexOf('--night=') === 0; })[0];
var nightEnabled = nightArg ? Number(nightArg.split('=')[1]) !== 0 : false;

var server = http.createServer(function(req, res) {
  var html = fs.readFileSync(SETTINGS_HTML, 'utf8'); // re-read each request so edits show on refresh
  var night = JSON.stringify({ enabled: nightEnabled, start: 22, end: 7 });
  html = html.replace('/*INIT*/', 'var REDRAW_MODE=' + redrawMode + ';var NIGHT=' + night + ';');
  res.writeHead(200, { 'Content-Type': 'text/html; charset=utf-8' });
  res.end(html);
});

server.listen(0, '127.0.0.1', function() {
  var url = 'http://127.0.0.1:' + server.address().port + '/';
  console.log('Serving settings.html at ' + url + ' — Ctrl-C to stop.');
  try { child_process.execFileSync('open', [url]); } catch (e) { /* open the URL yourself */ }
});
