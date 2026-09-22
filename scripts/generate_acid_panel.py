"""Render the 34HP release panel reference and precise production label overlay."""
import base64,json,math
from pathlib import Path
import cairosvg
from fontTools.ttLib import TTFont
from fontTools.pens.svgPathPen import SVGPathPen
R=Path(__file__).resolve().parents[1];O=R/'design/ACID9Voice';O.mkdir(parents=True,exist_ok=True)
C=Path('/Applications/VCV Rack 2 Pro.app/Contents/Resources/res/ComponentLibrary')
f=TTFont(R/'scripts/fonts/DejaVuSans.ttf');g=f.getGlyphSet();cm=f.getBestCmap();em=f['head'].unitsPerEm
W,H=510,380;boxes=[]
def width(s,z):return sum(g[cm[ord(c)]].width for c in s)*z/em
def text(s,x,y,z=7.2,color='#fff3df'):
 w=width(s,z);boxes.append([x-w/2,y-z,w,z+1]);x-=w/2;out=[]
 for c in s:
  q=g[cm[ord(c)]];p=SVGPathPen(g);q.draw(p);t=f'translate({x} {y}) scale({z/em} {-z/em})';out.append(f'<path d="{p.getCommands()}" transform="{t}" stroke="#080b16" stroke-width="{1.1*em/z}" fill="#080b16"/><path d="{p.getCommands()}" transform="{t}" fill="{color}"/>');x+=q.width*z/em
 return ''.join(out)
items=[]
for id,label,x,y,size,asset in [(16,'SLIDE TIME',170,155,22.6758,'RoundSmallBlackKnob'),(17,'ACCENT AMT',170,211,22.6758,'RoundSmallBlackKnob'),(15,'OCTAVE',125,211,22.6758,'RoundSmallBlackKnob'),(0,'SHAPE',45,92,44,'RoundBigBlackKnob'),(1,'ISOTOPE',125,92,44,'RoundBigBlackKnob'),(2,'SUB LEVEL',45,155,22.6758,'RoundSmallBlackKnob'),(3,'SUB MODE',125,155,14,'CKSS'),(4,'CUTOFF',215,92,44,'RoundBigBlackKnob'),(5,'RESONANCE',295,92,22.6758,'RoundSmallBlackKnob'),(6,'ENV MOD',215,155,22.6758,'RoundSmallBlackKnob'),(7,'DECAY',295,155,22.6758,'RoundSmallBlackKnob'),(9,'GRIT',215,211,22.6758,'RoundSmallBlackKnob'),(8,'FILTER MORPH',295,211,22.6758,'RoundSmallBlackKnob'),(10,'DELAY TIME',385,92,28.3476,'RoundBlackKnob'),(11,'FEEDBACK',465,92,22.6758,'RoundSmallBlackKnob'),(12,'DELAY MIX',385,155,22.6758,'RoundSmallBlackKnob'),(14,'GHOST',465,155,22.6758,'RoundSmallBlackKnob'),(13,'SYNC RATIO',425,211,22.6758,'RoundSmallBlackKnob')]:
 items.append(dict(kind='param',id=id,label=label,x=x,y=y,w=size,h=20.6411 if asset=='CKSS' else size,asset=asset))
for id,label,x,y in [(0,'V/OCT',45,320),(1,'GATE',125,320),(2,'ACCENT',205,320),(3,'SLIDE',285,320),(4,'CUTOFF CV',215,270),(5,'FM IN',45,211),(6,'CLOCK',385,270),(7,'INSERT RETURN',465,270),(8,'SHAPE CV',45,270),(9,'ISOTOPE CV',125,270),(10,'DECAY CV',295,270)]:
 items.append(dict(kind='input',id=id,label=label,x=x,y=y,w=23.7,h=23.7,asset='PJ301M'))
for id,label,x in [(0,'LEFT OUT',365),(1,'RIGHT OUT',425),(2,'INSERT SEND',485)]:
 items.append(dict(kind='output',id=id,label=label,x=x,y=320,w=23.7,h=23.7,asset='PJ301M'))
labels=''
for q in items:labels+=text(q['label'],q['x'],q['y']-q['h']/2-5,7 if q['kind']=='output' else 7.2,'#ffdb9a' if q['kind']=='output' else '#fff3df')
for s,x,y,z in [('OSCILLATOR',85,56,8),('FILTER',255,56,8),('DELAY',425,56,8),('SINE / SQUARE',125,178,6.5),('-3   OCT   +3',125,234,6.5),('ACID / LEAD',295,234,6.5),('/4   /2   ×1   ×2   ×4',425,234,6.5)]:labels+=text(s,x,y,z,'#b8d8e1')
labelboxes=boxes[:];placeholder=text('ACID9 VOICE',255,36,23,'#ffdc9e')+text('WIGGLE ROOM',255,365,13,'#ffdc9e');reserved=[[75,10,360,34],[155,346,200,25]]
def img(p,b):
 x,y,w,h=b;typ='image/png' if p.suffix=='.png' else 'image/svg+xml';return f'<image x="{x}" y="{y}" width="{w}" height="{h}" href="data:{typ};base64,{base64.b64encode(p.read_bytes()).decode()}"/>'
def svg(s):return f'<svg xmlns="http://www.w3.org/2000/svg" width="510" height="380" viewBox="0 0 510 380">{s}</svg>'
hardware='';controlboxes=[]
for q in items:
 b=[q['x']-q['w']/2,q['y']-q['h']/2,q['w'],q['h']];controlboxes.append(b);a=q['asset'];fn=a+'_0.svg' if a=='CKSS' else a+'.svg'
 if (C/(a+'_bg.svg')).exists():hardware+=img(C/(a+'_bg.svg'),b)
 hardware+=img(C/fn,b)
hardware+='<circle cx="310" cy="211" r="2.95" fill="#31584c"/>';controlboxes.append([307.05,208.05,5.9,5.9])
for x,y in [(5,3),(490,3),(5,362),(490,362)]:hardware+=img(C/'ScrewSilver.svg',[x,y,15,15]);controlboxes.append([x,y,15,15])
def hit(a,b):return a[0]<b[0]+b[2] and a[0]+a[2]>b[0] and a[1]<b[1]+b[3] and a[1]+a[3]>b[1]
allboxes=labelboxes+controlboxes+reserved
for i,a in enumerate(allboxes):
 assert a[0]>=0 and a[1]>=0 and a[0]+a[2]<=W and a[1]+a[3]<=H,a
 for b in allboxes[i+1:]:assert not hit(a,b),(a,b)
# Solid text shadow only, Rack-compatible SVG. Do not rely on unsupported SVG filters.
(R/'res/ACID9Voice-labels.svg').write_text(svg(labels))
for name,s in [('layout','<rect width="510" height="380" fill="#111322"/>'+labels+placeholder+hardware),('guide','<rect width="510" height="380" fill="#54315f"/>'+''.join(f'<rect x="{x-2}" y="{y-2}" width="{w+4}" height="{h+4}" rx="4" fill="#080b16"/>' for x,y,w,h in allboxes))]:
 (O/(name+'.svg')).write_text(svg(s));cairosvg.svg2png(bytestring=svg(s).encode(),write_to=str(O/(name+'.png')),scale=3)
(O/'layout.json').write_text(json.dumps(dict(width=510,height=380,hp=34,controls=items,light=[310,211],title_box=reserved[0],brand_box=reserved[1]),indent=2)+'\n')
art=R/'res/ACID9Voice.png'
if art.exists():
 s=svg(img(art,[0,0,W,H])+labels+hardware);(O/'preview.svg').write_text(s);cairosvg.svg2png(bytestring=s.encode(),write_to=str(O/'preview.png'),scale=3);cairosvg.svg2png(bytestring=s.encode(),write_to=str(O/'native.png'))
print('PASS: 32 controls, labels, mode legends, indicator and branding clearances.')
