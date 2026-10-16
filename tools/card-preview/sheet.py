from PIL import Image
import struct
imgs=[]
for i in range(1,7):
    d=open(f'c{i}.raw','rb').read(); w,h=struct.unpack('<ii',d[:8]); px=d[8:]
    imgs.append(Image.frombytes('RGBA',(w,h),px,'raw','BGRA'))
w,h=imgs[0].size; pad=24
sheet=Image.new('RGB',(2*w+3*pad,3*h+4*pad),(235,235,238))
for k,im in enumerate(imgs):
    sheet.paste(im.convert('RGB'),(pad+(k%2)*(w+pad),pad+(k//2)*(h+pad)))
sheet.save('sheet.png')
