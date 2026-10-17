# Card preview (Linux, development only)

Renders the in-cab screen cards (`RenderScreenCard` from `src/OverlayImage.cpp`) without Windows, so the layout can be checked.
`shim.h` / `shim.cpp` replace the few GDI text functions with FreeType and the Inter font; text widths therefore differ
slightly from Segoe UI in the game. Not part of the plugin.

```sh
cd tools/card-preview
python3 extract.py && python3 make_inputs.py
g++ -std=c++17 -O2 -w main.cpp shim.cpp $(pkg-config --cflags --libs freetype2) -o preview
./preview && python3 sheet.py && python3 menusheet.py   # -> sheet.png, menu.png
```
