# Copies the in-cab card renderer out of src/OverlayImage.cpp so it can be compiled on Linux with shim.h (FreeType).
import os
root = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'src')
src = open(os.path.join(root, 'OverlayImage.cpp'), encoding='utf-8').read()
hdr = open(os.path.join(root, 'OverlayImage.h'), encoding='utf-8').read()
structs = hdr[hdr.index('struct OverlayImage {'):hdr.index('// Draws the panel with GDI')]
a = src[src.index('namespace {'):src.index('std::shared_ptr<OverlayImage> RenderOverlay(')]
b = src[src.index('namespace {\nvoid FillRect'):src.index('std::shared_ptr<OverlayImage> RenderDisplay(')]
c = src[src.index('// In-cab screen card (1.1.0 design)'):src.index('std::shared_ptr<OverlayImage> RenderScreenProbe')]
open('card_preview.cpp', 'w', encoding='utf-8').write('#include "shim.h"\n' + structs + '\n' + a + '\n' + b + '\n' + c)
