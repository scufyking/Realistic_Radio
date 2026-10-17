from PIL import Image
import struct
def load(n):
    d=open(n+'.raw','rb').read(); w,h=struct.unpack('<ii',d[:8])
    return Image.frombytes('RGBA',(w,h),d[8:],'raw','BGRA')
imgs=[load('menu1'),load('menu2')]; cur=load('cursor')
w,h=imgs[0].size
bg=Image.new('RGBA',(w+40,2*h+60),(110,125,140,255))
for k,im in enumerate(imgs): bg.alpha_composite(im,(20,20+k*(h+20)))
bg.alpha_composite(cur,(20+int(w*0.78),20+int(h*0.92)))
bg.convert('RGB').save('menu.png')
