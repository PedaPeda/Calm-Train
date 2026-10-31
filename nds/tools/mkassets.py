from PIL import Image
im=Image.open('/home/claude/blocksds/libs/libnds/graphics/default_font.png').convert('L')
glyph={}
for c in range(32,128):
    i=c-32; gx=(i%32)*8; gy=(i//32)*8
    rows=[]
    for y in range(8):
        b=0
        for x in range(8):
            if im.getpixel((gx+x,gy+y))>128: b|=0x80>>x
        rows.append(b)
    glyph[c]=rows
def g(s):
    return [int(r.replace('.','0').replace('#','1'),2) for r in s.split()]
X={
128:g("##...##. ........ ##...##. ##...##. ##...##. ##...##. .#####.. ........"), # Ü
129:g("##...... ####.... ######.. #######. ######.. ####.... ##...... ........"), # ▶
130:g("........ ..###... .#####.. .#####.. .#####.. ..###... ........ ........"), # ●
131:g("........ ........ ........ ...##... ...##... ........ ........ ........"), # ·
132:g("######.. ######.. ######.. ######.. ######.. ######.. ######.. ........"), # ▮
133:g("######.. #....#.. #....#.. #....#.. #....#.. #....#.. ######.. ........"), # ▯
134:g("...##... ...###.. ...##.#. ...##... .####... #####... .###.... ........"), # ♪
135:g("..###... .##.##.. ###.###. ###.###. ###.###. .##.##.. ..###... ........"), # ◐ (approx)
136:g("#######. #.....#. #.###.#. #.###.#. #.###.#. #.....#. #######. ........"), # ▣
137:g("##...##. ........ .#####.. ##...##. #######. ##...##. ##...##. ........"), # Ä
138:g("##...##. ........ .#####.. ##...##. ##...##. ##...##. .#####.. ........"), # Ö
139:g("........ ...#.... ...#.... .#####.. ...#.... ...#.... ........ ........"), # ✛ plus
}
# fix ◐: left half filled, right outline
X[135]=g("..####.. .###..#. ####...# ####...# ####...# .###..#. ..####.. ........")
for k,v in X.items(): glyph[k]=v
with open('source/font8.h','w') as f:
    f.write('// generated\nstatic const unsigned char font8[140][8]={\n')
    for c in range(140):
        r=glyph.get(c,[0]*8)
        f.write('{'+','.join('0x%02x'%b for b in r)+'},\n')
    f.write('};\n')
# icon 32x32, 16 colors: night train on snow
pal=[(3,6,14),(10,20,40),(20,42,76),(35,74,122),(63,114,170),(127,176,226),(212,235,255),(169,208,245)]
ic=Image.new('P',(32,32),1)
flat=[]
for p in pal: flat+=list(p)
flat+= [0]*(768-len(flat))
ic.putpalette(flat)
px=ic.load()
import random
random.seed(3)
for y in range(32):
    for x in range(32):
        px[x,y]= 0 if y<6 else (1 if y<14 else 2)
for _ in range(14):
    px[random.randrange(32),random.randrange(12)]=6
# moon
for y in range(32):
    for x in range(32):
        if (x-24)**2+(y-6)**2<=9: px[x,y]=6
# mountains
for x in range(32):
    h=14-int(4*abs(((x*7)%11)-5)/5)
    for y in range(h,18): px[x,y]=1
# snow ground
for y in range(22,32):
    for x in range(32): px[x,y]=5 if y>24 else 4
# train: loco + car
for y in range(14,22):
    for x in range(3,30): px[x,y]=3
for x in range(3,30): px[x,14]=7
for x in range(3,30): px[x,19]=7
for wx in (6,11,16,21):
    for y in range(16,18):
        for x in range(wx,wx+3): px[x,y]=6
for y in range(16,18):
    px[27,y]=6; px[28,y]=6
px[29,20]=6
for x in range(3,30,4): px[x,22]=0; px[x+1,22]=0
for x in range(3,30): px[x,23]=3
ic.save('icon.bmp')
