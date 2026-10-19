# Converts the tunnel databases of "ETS2 Tunnel Radio" (https://github.com/bohbotoly/ets2-tunnel-radio, MIT,
# (c) 2026 Tal (bohbotoly)) into the compact form embedded in realistic-radio.dll:
#   {"source": ..., "meta": {...}, "t": [[ax, az, bx, bz, halfWidth], ...]}   (world X/Z in metres, 0.1 m)
# usage: python3 convert-tunnels.py <ets2-tunnel-radio checkout> <output folder>
import json, os, sys
src, out = sys.argv[1], sys.argv[2]
for name, db, meta in [("tunnels-ets2.json", "tunnels-1.60.json", "tunnels-1.60.meta.json"),
                       ("tunnels-ets2-promods.json", "tunnels-1.60-promods-me.json", "tunnels-1.60-promods-me.meta.json")]:
    t = json.load(open(os.path.join(src, "db", db), encoding="utf-8"))
    m = json.load(open(os.path.join(src, "db", meta), encoding="utf-8"))
    rows = [[round(x["Ax"], 1), round(x["Az"], 1), round(x["Bx"], 1), round(x["Bz"], 1), round(x["HalfWidth"], 1)] for x in t]
    doc = {"source": "ETS2 Tunnel Radio by Tal (bohbotoly), MIT license - https://github.com/bohbotoly/ets2-tunnel-radio", "meta": m, "t": rows}
    with open(os.path.join(out, name), "w", encoding="utf-8") as f:
        json.dump(doc, f, separators=(",", ":"))
    print(name, len(rows), os.path.getsize(os.path.join(out, name)))
