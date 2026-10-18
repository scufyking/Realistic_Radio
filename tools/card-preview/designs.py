from PIL import Image
import struct
def load(n):
    d=open(n+'.raw','rb').read(); w,h=struct.unpack('<ii',d[:8])
    return Image.frombytes('RGBA',(w,h),d[8:],'raw','BGRA').convert('RGB')
cw,ch=410,205; pad=12
s=Image.new('RGB',(3*cw+4*pad,5*ch+6*pad),(225,225,230))
for k in range(5):
    for j,p in enumerate('def'):
        s.paste(load(p+str(k)).resize((cw,ch)),(pad+j*(cw+pad),pad+k*(ch+pad)))
s.save('designs.png')
t=load('test').resize((512,512)); t.save('test.png')
