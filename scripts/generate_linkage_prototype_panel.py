"""Restore the standard 8HP panel required by the unreleased Linkage widget."""
from pathlib import Path
from fontTools.ttLib import TTFont
from fontTools.pens.svgPathPen import SVGPathPen
ROOT=Path(__file__).resolve().parents[1]
font=TTFont(ROOT/'scripts/fonts/DejaVuSans.ttf');glyphs=font.getGlyphSet();cmap=font.getBestCmap();units=font['head'].unitsPerEm
parts=['<svg xmlns="http://www.w3.org/2000/svg" width="120" height="380" viewBox="0 0 120 380">','<rect width="120" height="380" fill="#141421"/>']
def label(text,x,y,size,color='#eeeeff'):
    scale=size/units;width=sum(glyphs[cmap[ord(c)]].width for c in text)*scale;at=x-width/2
    for c in text:
        g=glyphs[cmap[ord(c)]];p=SVGPathPen(glyphs);g.draw(p)
        parts.append(f'<path fill="{color}" transform="translate({at},{y}) scale({scale},{-scale})" d="{p.getCommands()}"/>');at+=g.width*scale
label('LINKAGE',60,28,13,'#9cdde8')
for text,x,y in [('TENSION',60,38),('SLACK',60,93),('DAMPING',60,143),('TENS CV',30,200),('SLACK CV',90,200),('GATE',60,250),('OUT',60,300)]:label(text,x,y,8)
label('WIGGLE ROOM',60,351,9,'#9cdde8');parts.append('</svg>');(ROOT/'res/Linkage.svg').write_text('\n'.join(parts)+'\n')
