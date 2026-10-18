# Copies the in-cab card renderer, the settings menu renderer and the test grid out of src/OverlayImage.cpp so they
# can be compiled on Linux with shim.h (FreeType).
import os
root = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'src')
src = open(os.path.join(root, 'OverlayImage.cpp'), encoding='utf-8').read()
hdr = open(os.path.join(root, 'OverlayImage.h'), encoding='utf-8').read()
structs = hdr[hdr.index('struct OverlayImage {'):hdr.index('// Draws the panel with GDI')]
menu_struct = hdr[hdr.index('enum class MenuItemKind'):hdr.index('std::shared_ptr<OverlayImage> RenderMenu(')]
decls = '''std::shared_ptr<OverlayImage> RenderScreenCard(const OverlayInfo& info, const OverlayImage* logo, int W, int H, const std::string& design = "modern", float scale = 1.f, float logoSize = 1.f);
std::shared_ptr<OverlayImage> RenderScreenGrid(int W, int H, bool flipX = false, bool flipY = false);
'''
a = src[src.index('namespace {'):src.index('std::shared_ptr<OverlayImage> RenderOverlay(')]
b = src[src.index('namespace {\nvoid FillRect'):src.index('std::shared_ptr<OverlayImage> RenderDisplay(')]
c = src[src.index('// In-cab screen card (1.1.0 design)'):src.index('std::shared_ptr<OverlayImage> RenderScreenProbe')]
g0 = src.index('std::shared_ptr<OverlayImage> RenderScreenGrid('); g = src[g0:src.index('\n}\n', g0) + 3]
m = src[src.index('// Settings menu (Ctrl+0). Layout'):]
open('card_preview.cpp', 'w', encoding='utf-8').write('#include "shim.h"\n' + structs + '\n' + menu_struct + '\n' + decls + a + '\n' + b + '\n' + c + '\n' + g + '\n' + m)
