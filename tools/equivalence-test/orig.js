// Runs the UNMODIFIED refresh() from the original web/lib/js/main.js with the original data files.
const fs = require('fs'), vm = require('vm');
const web = (process.argv[3] || 'web') + '/';  // path to the reference web folder
const chain = new Proxy(function () {}, { get: (t, p) => p === 'then' ? undefined : chain, apply: () => chain });
const $ = new Proxy(function () { return chain; }, { get: (t, p) => p === 'extend' ? Object.assign : (p === 'isEmptyObject' ? (o => !o || Object.keys(o).length === 0) : () => chain) });
const ctx = vm.createContext({ $, jQuery: $, console, document: { getElementById: () => ({ paused: true, volume: 0, play() {}, pause() {} }) },
  localStorage: { getItem: () => null, setItem() {} }, location: { hash: '', protocol: 'http:' }, setTimeout() {}, setInterval() {}, window: {}, navigator: { userAgent: '' } });
for (const f of ['config.js', 'cities/cities-ets2.js', 'stations/stations-europe.js', 'lib/js/main.js'])
  vm.runInContext(fs.readFileSync(web + f, 'utf8').replace(/^﻿/, ''), ctx, { filename: f });
vm.runInContext('g_skinConfig.radius = 7500; refreshStations = function(){}; setWhitenoise = function(){};', ctx);
const pts = fs.readFileSync(process.argv[2], 'utf8').trim().split('\n').map(l => l.split(' ').map(Number));
const out = [];
for (const [x, z] of pts) {
  vm.runInContext(`g_countries = {}; g_initial_received = false; g_current_country = null;
    refresh({TruckValues:{Positioning:{HeadPositionInWorldSpace:{X:${x},Y:0,Z:${z}}}}});`, ctx);
  const c = vm.runInContext('g_countries', ctx), best = vm.runInContext('g_last_nearest_country', ctx);
  out.push(best + Object.keys(c).filter(k => k !== 'global').sort().map(k => ` ${k}=${c[k].whitenoise.toFixed(6)}`).join(''));
}
console.log(out.join('\n'));
