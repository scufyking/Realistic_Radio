from PIL import Image
import struct
def load(n):
    d=open(n+'.raw','rb').read(); w,h=struct.unpack('<ii',d[:8])
    return Image.frombytes('RGBA',(w,h),d[8:],'raw','BGRA')
a,b=load('menu3'),load('menu4'); w,h=a.size
bg=Image.new('RGBA',(w+40,2*h+60),(110,125,140,255)); bg.alpha_composite(a,(20,20)); bg.alpha_composite(b,(20,40+h)); bg.convert('RGB').save('menu2.png')
ts=[load('t%d'%k) for k in range(6)]; tw,th=ts[0].size; tw2,th2=tw//2,th//2
s=Image.new('RGB',(3*tw2+40,2*th2+30),(230,230,233))
for k,t in enumerate(ts): s.paste(t.convert('RGB').resize((tw2,th2)),(10+(k%3)*(tw2+10),10+(k//3)*(th2+10)))
s.save('types.png')
