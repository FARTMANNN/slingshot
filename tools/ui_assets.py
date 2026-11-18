#!/usr/bin/env python3
"""Slingshot: builds the in-game menu's look from the player's own game files.

    python3 dm/slingshot/tools/ui_assets.py <romfs folder> <output folder>

It reads the game's UI fonts (Font2/Scalable/*.bfotf.zs, a 32-bit XOR over the OpenType file) and the common UI
textures (UI/Layout2Archive/Common.*.bntx.zs, Switch 2 block-linear ASTC / BC4) and writes, into the output folder:
fonts (CetraDisplayPro-Regular / -Medium, NinDev-NumberDeco, External: the controller glyphs) and PNGs the menu
draws (front_mac.m, slingshot/src/menu.h), and AppIcon.png, the app's icon (the build's --icon). Nothing of the game is part of Slingshot itself: every player makes these
from their own copy. Needs: zstandard, texture2ddecoder, Pillow.
"""
import math, os, struct, sys
import zstandard, texture2ddecoder
from PIL import Image, ImageChops, ImageDraw, ImageFilter, ImageStat

# --- zstd with the game's dictionaries, and SARC -------------------------------------------------------------
def sarc(data):
    assert data[:4] == b'SARC'
    hdr = struct.unpack_from('<H', data, 4)[0]; base = struct.unpack_from('<I', data, 0xC)[0]
    assert data[hdr:hdr + 4] == b'SFAT'
    count = struct.unpack_from('<H', data, hdr + 6)[0]
    nodes = [struct.unpack_from('<IIII', data, hdr + 12 + 16 * i) for i in range(count)]
    names = hdr + 12 + 16 * count + 8
    out = {}
    for _, attr, s, e in nodes:
        o = names + (attr & 0xFFFF) * 4
        out[data[o:data.index(b'\0', o)].decode()] = data[base + s: base + e]
    return out

class Romfs:
    def __init__(self, root):
        self.root = root
        raw = zstandard.ZstdDecompressor().decompress(open(os.path.join(root, 'Pack/ZsDic.pack.zs'), 'rb').read(), max_output_size=1 << 28)
        self.dicts = [zstandard.ZstdCompressionDict(d) for d in sarc(raw).values()]
    def read(self, rel):
        raw = open(os.path.join(self.root, rel), 'rb').read()
        for d in [None] + self.dicts:
            try:
                return (zstandard.ZstdDecompressor(dict_data=d) if d else zstandard.ZstdDecompressor()).decompress(raw, max_output_size=1 << 30)
            except zstandard.ZstdError:
                pass
        raise RuntimeError('cannot decompress ' + rel)

# --- fonts ---------------------------------------------------------------------------------------------------
FONT_KEY = 0xa6018502
def font(data):
    n = len(data) // 4 * 4
    w = struct.unpack('>%dI' % (n // 4), data[:n])
    dec = struct.pack('>%dI' % len(w), *[x ^ FONT_KEY for x in w])
    size = struct.unpack('>I', dec[4:8])[0]
    body = dec[8:8 + size]
    assert body[:4] in (b'OTTO', b'\0\1\0\0'), 'not a font after the XOR'
    return body

# --- BNTX (platform "Ounc") ----------------------------------------------------------------------------------
def u16(d, o): return struct.unpack_from('<H', d, o)[0]
def u32(d, o): return struct.unpack_from('<I', d, o)[0]
def u64(d, o): return struct.unpack_from('<Q', d, o)[0]
def bntx_textures(d):
    assert d[:4] == b'BNTX' and d[0x20:0x24] in (b'NX  ', b'Ounc')
    out = {}
    for i in range(u32(d, 0x24)):
        b = u64(d, u64(d, 0x28) + 8 * i); assert d[b:b + 4] == b'BRTI'
        no = u64(d, b + 0x60); name = d[no + 2: no + 2 + u16(d, no)].decode()
        out[name] = dict(fmt=u32(d, b + 0x1C), w=u32(d, b + 0x24), h=u32(d, b + 0x28), layout=u32(d, b + 0x34), size=u32(d, b + 0x50),
                         chan=d[b + 0x58: b + 0x5C], data=u64(d, u64(d, b + 0x70)))
    return out
BPB = {0x1d01: 8, 0x2d01: 16, 0x2d06: 16}
def deswizzle(src, wb, hb, bpb, layout):
    # Switch 2 block-linear: 512-byte groups of 64 bytes x 8 block rows, (1 << layout) groups stacked in a block
    row = wb * bpb; bh = 1 << layout
    while bh > 1 and hb <= (bh // 2) * 8: bh //= 2
    bxn = (row + 63) // 64; out = bytearray(row * hb)
    for y in range(hb):
        by, gy, yy = y // (8 * bh), (y % (8 * bh)) // 8, y % 8
        base = by * bxn * 512 * bh + gy * 512 + (yy // 4) * 128 + (yy % 4) * 16
        for x in range(0, row, 16):
            xx = x % 64; a = base + (x // 64) * 512 * bh + (xx // 32) * 256 + ((xx % 32) // 16) * 64; n = min(16, row - x)
            out[y * row + x: y * row + x + n] = src[a:a + n]
    return bytes(out)
def texture(d, t):
    bpb = BPB[t['fmt']]; wb, hb = (t['w'] + 3) // 4, (t['h'] + 3) // 4
    lin = deswizzle(d[t['data']: t['data'] + t['size']], wb, hb, bpb, t['layout'])
    bgra = texture2ddecoder.decode_bc4(lin, t['w'], t['h']) if t['fmt'] == 0x1d01 else texture2ddecoder.decode_astc(lin, t['w'], t['h'], 4, 4)
    im = Image.frombytes('RGBA', (t['w'], t['h']), bgra, 'raw', 'BGRA'); ch = im.split()
    src = {0: Image.new('L', im.size, 0), 1: Image.new('L', im.size, 255), 2: ch[0], 3: ch[1], 4: ch[2], 5: ch[3]}
    return Image.merge('RGBA', [src[c] for c in t['chan']])

# --- composites ----------------------------------------------------------------------------------------------
def tint(mask, rgb, alpha=1.0):
    """A colour with the mask's alpha (an alpha-only texture's A, or the luminance of a white one) times alpha"""
    a = mask.split()[3] if mask.mode == 'RGBA' else mask
    if alpha != 1: a = a.point(lambda v: int(v * alpha))
    out = Image.new('RGBA', a.size, rgb + (255,)); out.putalpha(a); return out
def over(*layers):
    out = Image.new('RGBA', layers[0].size, (0, 0, 0, 0))
    for l in layers: out.alpha_composite(l.resize(out.size) if l.size != out.size else l)
    return out
def multiply(im, rgb):
    r, g, b, a = im.split()
    return Image.merge('RGBA', [r.point(lambda v: v * rgb[0] // 255), g.point(lambda v: v * rgb[1] // 255), b.point(lambda v: v * rgb[2] // 255), a])
def textured(base, noise, strength):
    """base darkened where the noise texture is dark (the game's grain on its brown surfaces)"""
    n = noise.split()[0].resize(base.size).point(lambda v: 255 - int((255 - v) * strength))
    r, g, b, a = base.split()
    return Image.merge('RGBA', [ImageChops.multiply(c, n) for c in (r, g, b)] + [a])

FILL = (103, 82, 48)      # the game's button brown (System menu)
LINE = (166, 146, 104)    # its ornament line
CREAM = (242, 236, 222)

# --- the app icon: the game's Ocarina of Time on a warm glow, in the macOS icon shape (a 1024 canvas, the shape 824 wide, a soft shadow) ---------
def squircle(size, box, n=5.0, ss=4):
    """A mask of the macOS icon shape (a superellipse) filling box = (x0, y0, x1, y1) of a size x size canvas"""
    S = size * ss; x0, y0, x1, y1 = [v * ss for v in box]; cx, cy, rx, ry = (x0 + x1) / 2, (y0 + y1) / 2, (x1 - x0) / 2, (y1 - y0) / 2
    pts = []
    for i in range(4000):
        t = 2 * math.pi * i / 4000; c, s = math.cos(t), math.sin(t)
        pts.append((cx + rx * math.copysign(abs(c) ** (2 / n), c), cy + ry * math.copysign(abs(s) ** (2 / n), s)))
    m = Image.new('L', (S, S), 0); ImageDraw.Draw(m).polygon(pts, fill=255)
    return m.resize((size, size), Image.LANCZOS)
def radial(size, centre, radius, inner, outer, power=1.0):
    """An RGBA radial gradient from inner (at centre) to outer (at radius and beyond)"""
    small = 256; im = Image.new('RGBA', (small, small)); px = im.load(); k = size / small
    for y in range(small):
        for x in range(small):
            d = min(1.0, math.hypot(x * k - centre[0], y * k - centre[1]) / radius) ** power
            px[x, y] = tuple(int(inner[i] + (outer[i] - inner[i]) * d) for i in range(4))
    return im.resize((size, size), Image.BICUBIC)
def star(size, xy, r, colour):
    """A four-pointed sparkle with a glow"""
    layer = Image.new('RGBA', (size, size), (0, 0, 0, 0)); d = ImageDraw.Draw(layer); x, y = xy; w = r * 0.18
    d.polygon([(x, y - r), (x + w, y - w), (x + r, y), (x + w, y + w), (x, y + r), (x - w, y + w), (x - r, y), (x - w, y - w)], fill=colour + (255,))
    glow = Image.new('RGBA', (size, size), (0, 0, 0, 0)); ImageDraw.Draw(glow).ellipse([x - r * 0.9, y - r * 0.9, x + r * 0.9, y + r * 0.9], fill=colour + (150,))
    return over(glow.filter(ImageFilter.GaussianBlur(r * 0.6)), layer)
def app_icon(rom, paper_tex, size=1024):
    item = lambda sub, name: texture(*(lambda d: (d, bntx_textures(d)[name]))(rom.read('UI/Tex/%s/%s.bntx.zs' % (sub, name))))
    box = (100, 100, 924, 924); shape = squircle(size, box)
    # the plate: a warm parchment glow in the middle going to dark wood at the edges, with the map paper's grain
    plate = radial(size, (512, 470), 560, (246, 204, 118, 255), (58, 30, 12, 255), 1.1)
    grain = paper_tex.convert('RGBA').resize((size, size), Image.LANCZOS).convert('L').point(lambda v: 175 + v * 80 // 255)
    plate = Image.merge('RGBA', [ImageChops.multiply(c, grain) for c in plate.split()[:3]] + [Image.new('L', (size, size), 255)])
    # light rays behind the ocarina, fading out from the middle
    rays = Image.new('L', (size, size), 0); d = ImageDraw.Draw(rays)
    for i in range(16):
        a = 2 * math.pi * i / 16 + 0.1; w = 0.07
        d.polygon([(512, 480), (512 + 900 * math.cos(a - w), 480 + 900 * math.sin(a - w)), (512 + 900 * math.cos(a + w), 480 + 900 * math.sin(a + w))], fill=255)
    rays = ImageChops.multiply(rays.filter(ImageFilter.GaussianBlur(10)), radial(size, (512, 480), 470, (90, 90, 90, 255), (0, 0, 0, 255), 0.9).split()[0])
    plate.alpha_composite(tint(rays, (255, 236, 190)))
    plate.alpha_composite(radial(size, (512, 480), 330, (255, 244, 214, 140), (255, 244, 214, 0), 1.6))
    # the ocarina, with the game's own shadow under it
    oc, sh = item('Icon', 'Item_Ocarina_Time'), item('IconSh', 'Item_Ocarina_Time_Sh')
    s = 680; o = (512 - s // 2, 480 - s // 2)
    shadow = sh.resize((s, s), Image.LANCZOS); shadow.putalpha(shadow.split()[3].point(lambda v: int(v * 0.55)))
    plate.alpha_composite(shadow, (o[0] + 28, o[1] + 46))
    plate.alpha_composite(oc.resize((s, s), Image.LANCZOS), o)
    for xy, r in (((280, 290), 32), ((752, 690), 24), ((322, 730), 16), ((730, 270), 14)):
        plate.alpha_composite(star(size, xy, r, (255, 248, 226)))
    # the rim: a gold edge and a thin dark line inside it, then a soft shade at the bottom for depth
    rim = ImageChops.subtract(shape, shape.filter(ImageFilter.MinFilter(17)))
    plate.alpha_composite(over(tint(rim, (0, 0, 0)), Image.composite(radial(size, (512, 160), 900, (250, 222, 150, 255), (138, 98, 44, 255)), Image.new('RGBA', (size, size), (0, 0, 0, 0)), rim)))
    inner = ImageChops.subtract(shape.filter(ImageFilter.MinFilter(17)), shape.filter(ImageFilter.MinFilter(23)))
    plate.alpha_composite(tint(inner, (30, 18, 8), 0.7))
    plate.putalpha(shape)
    out = Image.new('RGBA', (size, size), (0, 0, 0, 0))
    drop = Image.new('L', (size, size), 0); drop.paste(shape, (0, 14)); out.alpha_composite(tint(drop.filter(ImageFilter.GaussianBlur(14)), (0, 0, 0), 0.45))
    out.alpha_composite(plate)
    return out

def build(rom, out):
    os.makedirs(out, exist_ok=True)
    for f in ('CetraDisplayPro-Regular', 'CetraDisplayPro-Medium', 'NinDev-NumberDeco', 'External'):
        open(os.path.join(out, f + '.otf'), 'wb').write(font(rom.read('Font2/Scalable/%s.Nin_Ounce_NVN2.bfotf.zs' % f)))
    d = rom.read('UI/Layout2Archive/Common.Product.100.Nin_Ounce_NVN2.bntx.zs'); ts = bntx_textures(d)
    T = lambda n: texture(d, ts[n])
    save = lambda im, n: im.save(os.path.join(out, n + '.png'))
    base, glow, orn, rect, noise = T('BtnThinBase_00'), T('BtnThinBase_01'), T('BtnThinBase_02'), T('BtnThinBase_04'), T('BtnThinBase_05')
    body = textured(tint(base, FILL), noise, 0.35)
    save(over(body, tint(orn, LINE, 0.9)), 'btn')
    thick = rect.split()[3].filter(ImageFilter.MaxFilter(7))
    save(over(tint(glow, (205, 225, 255), 0.95), body, tint(orn, LINE, 0.6), tint(thick, (255, 255, 255))), 'btn_sel')
    # the parchment: the map paper toned down to the System menu's, with burnt edges
    paper = T('LocalMapPaper_00').resize((1600, 1120), Image.LANCZOS)
    r, g, b, a = paper.convert('RGBA').split(); mean = ImageStat.Stat(paper.convert('RGB')).mean
    target = (122, 97, 56)
    paper = Image.merge('RGBA', [c.point(lambda v, k=target[i] / mean[i]: min(255, int(v * k))) for i, c in enumerate((r, g, b))] + [a])
    burn = T('CmnPaperWindowBurn_00').resize((1600, 1120), Image.LANCZOS).split()[0].point(lambda v: 90 + v * 165 // 255)
    paper = Image.merge('RGBA', [ImageChops.multiply(c, burn) for c in paper.split()[:3]] + [Image.new('L', paper.size, 255)])
    save(paper, 'parchment')
    # the left column: dark wood with the game's grain
    col = Image.new('RGBA', (660, 1120), (36, 26, 17, 245))   # drawn 330 x 560 at 1280 x 720
    save(textured(col, T('DecoNoise_00').resize((660, 1120)), 0.45), 'column')
    # bars: a settings row (the game's label bar, dark under the label) and the button guide's base (fading to the left)
    g0 = T('SystemGuideBase_00')   # dark at the left, where a row's label is, fading out to the right
    save(tint(g0, (44, 33, 21), 0.9), 'bar')
    save(tint(g0, (150, 120, 72), 0.95), 'bar_sel')
    save(tint(T('SystemGuideBase_02').transpose(Image.FLIP_LEFT_RIGHT), (8, 6, 4), 0.8), 'guide')
    tab = T('MenuHeaderSelect_00')
    save(textured(multiply(tab, (150, 116, 70)), noise, 0.2), 'tab')
    line = T('SystemLine_00')
    save(tint(line, (44, 30, 20)), 'flourish_dark'); save(tint(line, CREAM, 0.9), 'flourish')
    arrow = tint(T('MenuArrow_00'), CREAM)
    save(arrow, 'arrow_left'); save(arrow.transpose(Image.FLIP_LEFT_RIGHT), 'arrow_right')
    save(app_icon(rom, T('LocalMapPaper_00')), 'AppIcon')   # the build's --icon
    open(os.path.join(out, 'VERSION'), 'w').write('1\n')

if __name__ == '__main__':
    if len(sys.argv) != 3: sys.exit(__doc__)
    build(Romfs(sys.argv[1]), sys.argv[2]); print('Slingshot UI assets written to', sys.argv[2])
