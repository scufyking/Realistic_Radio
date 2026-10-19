# Third-party notices / Hinweise zu Drittkomponenten

Realistic Radio © 2026 ScufyKing, licensed under the Mozilla Public License 2.0 (see `LICENSE`).

This project contains or is derived from the following third-party works. Their notices are reproduced here as required by their licenses.
Dieses Projekt enthält folgende Drittkomponenten bzw. ist teilweise davon abgeleitet. Die Hinweise stehen hier, weil die jeweiligen Lizenzen es verlangen.

---

## Station data, city data, sounds and station selection logic

Files: `data/ets2.json`, `data/ats.json`, `data/audio/whitestatic.mp3`, `data/audio/radio_switch.mp3`, `server/logo/*`;
logic in `src/RadioManager.*`, `src/ReceptionCalculator.*`, `src/StationDatabase.*`.

Derived from **ETS2 Local Radio**
Copyright (c) Koen van Hove and contributors
Licensed under the Mozilla Public License 2.0 — https://mozilla.org/MPL/2.0/
Source: https://github.com/Koenvh1/ets2-local-radio

The data was converted to JSON and the logos were downscaled; the logic was reimplemented in C++.
This project is independent and not affiliated with or endorsed by the authors of that work.

Station logos are trademarks of their respective broadcasters and are used only to identify the stations.
Radio streams are provided by their respective broadcasters.

---

## Tunnel databases — ETS2 Tunnel Radio (`data/tunnels/`)

Files: `data/tunnels/tunnels-ets2.json`, `data/tunnels/tunnels-ets2-promods.json` (converted with `tools/convert-tunnels.py`,
embedded in the DLL); the check whether the truck is inside a tunnel (`src/TunnelMap.*`) follows the same method.

From **ETS2 Tunnel Radio** by Tal (bohbotoly) — https://github.com/bohbotoly/ets2-tunnel-radio
(databases: ETS2 1.60 + map DLCs, and ProMods 2.83 + Middle-East add-on; built from the game's "no weather" zones).

MIT License

Copyright (c) 2026 Tal (bohbotoly)

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

---

## Truck logos (`data/truck-logos/`)

The logo pictures for the start screen (DAF, Iveco, MAN, Mercedes-Benz, Scania, Volvo) were prepared by ScufyKing and are embedded in `realistic-radio.dll`. The brand names and logos are trademarks of their respective owners; they are used only to show the manufacturer of the driven truck. Realistic Radio is not affiliated with, endorsed or sponsored by any of these manufacturers or by SCS Software. The MPL-2.0 license of this project does not grant any rights to these trademarks.

## SCS Telemetry SDK (`sdk/`)

Copyright (C) 2016 SCS Software

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

---

## miniaudio 0.11.25 (`third_party/miniaudio.*`)

Copyright (c) David Reid. Public domain or MIT No Attribution (MIT-0), at your choice. Full text: `third_party/LICENSE-miniaudio`.
https://github.com/mackron/miniaudio

---

## nlohmann/json 3.11.3 (`third_party/json.hpp`)

Copyright (c) 2013-2023 Niels Lohmann. Licensed under the MIT License:

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
https://github.com/nlohmann/json

---

## Adafruit GFX Library — 5×7 font (`third_party/font5x7.h`)

Software License Agreement (BSD License)

Copyright (c) 2012 Adafruit Industries.  All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

- Redistributions of source code must retain the above copyright notice,
  this list of conditions and the following disclaimer.
- Redistributions in binary form must reproduce the above copyright notice,
  this list of conditions and the following disclaimer in the documentation
  and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.
