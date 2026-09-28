#!/usr/bin/env node
var assert = require('assert');
var fs = require('fs');
var vm = require('vm');
var handlers = {};
var sent;
var context = {
  Pebble: {
    addEventListener: function(name, fn) { handlers[name] = fn; },
    openURL: function() {},
    sendAppMessage: function(message) { sent = message; },
  },
  localStorage: { getItem: function() { return null; }, setItem: function() {} },
  JSON: JSON, Number: Number, Math: Math, decodeURIComponent: decodeURIComponent,
};
vm.runInNewContext(fs.readFileSync('src/pkjs/index.js', 'utf8'), context);
handlers.webviewclosed({ response: encodeURIComponent(JSON.stringify({
  nightEnabled: 1, nightStart: -4, nightEnd: 99,
})) });
assert.strictEqual(sent.NightEnabled, 1);
assert.strictEqual(sent.NightStart, 0);
assert.strictEqual(sent.NightEnd, 23);

var wrapper = fs.readFileSync('src/pkjs/settings-html.js', 'utf8');
assert(wrapper.indexOf('\\u00b7') !== -1);
assert(wrapper.indexOf('\\u00c2\\u00b7') === -1);
