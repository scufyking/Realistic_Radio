// ETS2 Local Radio Native
// Copyright (c) 2026 ScufyKing
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
// Converts the original ETS2 Local Radio web data (cities-*.js, stations-*.js) into plain JSON
// for the native DLL. Build-time only; end users never need Node.js.
// Usage: node convert_data.js <web-dir> <out-dir>
const fs = require('fs'), path = require('path'), vm = require('vm');
const [web, out] = process.argv.slice(2);
function load(files) {
  const $ = { extend: (...a) => Object.assign(...a) };
  const ctx = vm.createContext({ $, jQuery: $, console });
  for (const f of files) vm.runInContext(fs.readFileSync(f, 'utf8').replace(/^﻿/, '').replace(/^(\s*)(let|const) /gm, '$1var '), ctx, { filename: f });
  return ctx;
}
for (const [game, citiesF, stationsF] of [['ets2', 'cities-ets2.js', 'stations-europe.js'], ['ats', 'cities-ats.js', 'stations-america.js']]) {
  const c = load([path.join(web, 'cities', citiesF), path.join(web, 'stations', stationsF)]);
  const cities = c.cities.map(x => ({ gameName: x.gameName, realName: x.realName, country: String(x.country).toLowerCase(), x: +x.x, z: +x.z }));
  const stations = {};
  for (const k of Object.keys(c.stations)) stations[k.toLowerCase()] = c.stations[k].map(s => ({ name: s.name, url: s.url || '', logo: s.logo || '', ...(s.relative_radius !== undefined ? { relative_radius: +s.relative_radius } : {}) }));
  const countries = {};
  for (const k of Object.keys(c.country_properties)) { const p = c.country_properties[k]; countries[k.toLowerCase()] = { name: p.name_english || p.name || k, native_name: p.name || p.name_english || k, relative_radius: p.relative_radius !== undefined ? +p.relative_radius : 1 }; }
  const cityProps = {};
  for (const k of Object.keys(c.city_properties || {})) { const p = c.city_properties[k]; const o = {}; if (p.relative_radius !== undefined) o.relative_radius = +p.relative_radius; if (p.relative_whitenoise !== undefined) o.relative_whitenoise = +p.relative_whitenoise; cityProps[k] = o; }
  fs.writeFileSync(path.join(out, `${game}.json`), JSON.stringify({ game, source: `${citiesF} + ${stationsF} (MPL-2.0 data, see NOTICE.md)`, countries, city_properties: cityProps, cities, stations }));
  console.log(game, 'cities', cities.length, 'countries', Object.keys(countries).length, 'station-countries', Object.keys(stations).length);
}
