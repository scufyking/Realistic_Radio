# Writes logo.raw (a station logo from server/logo) and cover.raw (a made-up album cover) as premultiplied BGRA.
import os, struct, sys
from PIL import Image, ImageDraw
def save(img, path):
    img = img.convert('RGBA'); w, h = img.size
    data = bytearray(struct.pack('<ii', w, h))
    for r, g, b, a in img.getdata(): data += bytes([b * a // 255, g * a // 255, r * a // 255, a])
    open(path, 'wb').write(data)
here = os.path.dirname(os.path.abspath(__file__))
save(Image.open(os.path.join(here, '..', '..', 'server', 'logo', 'stations', 'images-europe', 'germany', 'ROCK ANTENNE.png')), 'logo.raw')
c = Image.new('RGB', (400, 400)); d = ImageDraw.Draw(c)
for y in range(400): d.line([(0, y), (400, y)], fill=(30 + y // 4, 60 + y // 8, 140 - y // 5))
d.ellipse([90, 90, 310, 310], outline=(255, 200, 80), width=14); d.ellipse([170, 170, 230, 230], fill=(255, 120, 60))
save(c, 'cover.raw')
