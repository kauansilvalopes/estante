from pathlib import Path
from PIL import Image, ImageDraw, ImageFont
import struct,hashlib
root=Path(__file__).resolve().parent.parent
source=root/'third_party/noto/NotoSans.ttf'
entries=[];data=bytearray()
for size in [24,28,32,44,64]:
 font=ImageFont.truetype(str(source),size)
 for code in list(range(32,127))+list(range(160,256))+[0x2026,0x2022,0x2190,0x2192,0x2713]:
  char=chr(code);l,t,rr,b=font.getbbox(char,anchor='ls');w,h=rr-l,b-t
  mask=Image.new('L',(max(w,1),max(h,1)));ImageDraw.Draw(mask).text((-l,-t),char,font=font,fill=255,anchor='ls')
  raw=mask.tobytes() if w*h else b''
  entries.append((code,size,len(data),l,t,w,h,round(font.getlength(char))));data.extend(raw)
result=struct.pack('<II',0x46545345,len(entries))+b''.join(struct.pack('<IIIiiIII',*e) for e in entries)+data
out=root/'assets/fonts';out.mkdir(exist_ok=True);(out/'noto.atlas').write_bytes(result)
print('Font atlas:',len(result),'bytes; source SHA256:',hashlib.sha256(source.read_bytes()).hexdigest())
