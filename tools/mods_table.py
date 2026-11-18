#!/usr/bin/env python3
"""Slingshot: gets the mods folder ready for the game.

    python3 dm/slingshot/tools/mods_table.py <game romfs> [<mods folder>]      (default: ~/Library/Application Support/Slingshot/mods)

Slingshot's mods are folders in the mods folder, each a RomFS overlay the game reads files from before its own (slingshot/src/mods.h):
  link-skins/skins/<id>/romfs/   Link Skins: one skin worn at a time, picked in the game's Slingshot menu (Mods)
  <any>/romfs/                   a plain RomFS mod, switched on or off there

The game reads its resource size table (System/Resource/ResourceSizeTable.*.rsizetable.zs) once, at its start, and a file larger than its entry there may
stop it. So that a skin can be put on while the game runs, this writes link-skins/table/romfs/System/Resource/<the game's table>: one table over every skin,
which Slingshot gives the game whether a skin is worn or not. Its rules are Link Skins 0.4.4's (github.com/FARTMANNN/links-skins, src/link_skins.c):
the game's table, raised by every other mod's table and by an entry per skin file: a texture's entry is its unpacked size + 280, a model no larger than the
game's keeps the game's entry, a larger or new file gets a guess with room to spare; skin.json "sizes" and a table inside a skin raise it further.
Run it again after adding or removing a skin. Needs zstandard.
"""
import json, os, struct, sys, zlib
import zstandard

TABLE_DIR = 'System/Resource'

def table_read(path):
    d = zstandard.ZstdDecompressor().decompress(open(path, 'rb').read(), max_output_size=256 << 20)
    assert d[:6] == b'RESTBL', path + ': not a size table'
    version, name_len, n, names = struct.unpack_from('<4I', d, 6)
    flat = struct.unpack_from('<%dI' % (2 * n), d, 0x16); rows = dict(zip(flat[0::2], flat[1::2]))
    named, at = {}, 0x16 + 8 * n
    for _ in range(names):
        raw = d[at:at + name_len]; named[raw.split(b'\0', 1)[0].decode()] = struct.unpack_from('<I', d, at + name_len)[0]; at += name_len + 4
    return {'version': version, 'name_len': name_len, 'rows': rows, 'named': named}

def table_write(t, path):
    rows = sorted(t['rows'].items()); out = bytearray(b'RESTBL') + struct.pack('<4I', t['version'], t['name_len'], len(rows), len(t['named']))
    for crc, size in rows: out += struct.pack('<2I', crc, size)
    for name, size in t['named'].items(): out += name.encode().ljust(t['name_len'], b'\0') + struct.pack('<I', size)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    open(path, 'wb').write(zstandard.ZstdCompressor(level=9).compress(bytes(out)))

def table_get(t, key):
    return t['named'][key] if key in t['named'] else t['rows'].get(zlib.crc32(key.encode()), 0)

def table_raise(t, key, size):
    if key in t['named']: t['named'][key] = max(t['named'][key], size)
    else: crc = zlib.crc32(key.encode()); t['rows'][crc] = max(t['rows'].get(crc, 0), size)

def table_merge(t, other):
    for crc, size in other['rows'].items(): t['rows'][crc] = max(t['rows'].get(crc, 0), size)
    for name, size in other['named'].items():
        t['named'][name] = max(t['named'].get(name, 0), size)   # (added when ours has no such row: the kit's merge keeps every named row)

def content_size(path):
    """The bytes a .zs file unpacks to (0 when its frame does not say); another file: its size"""
    if not path.endswith('.zs'): return os.path.getsize(path)
    with open(path, 'rb') as f: head = f.read(18)
    try: n = zstandard.get_frame_parameters(head).content_size
    except zstandard.ZstdError: return 0
    return 0 if n in (0, zstandard.CONTENTSIZE_UNKNOWN, zstandard.CONTENTSIZE_ERROR) else n

def clamp32(v): return 0xFFFFFFF0 if v > 0xFFFFFFF0 else (v + 7) & ~7

def needed_size(key, mine, base, orig):
    if key.endswith('.bntx'): return clamp32(mine + 280)
    if base and orig and mine <= orig: return base
    if base and orig: return clamp32(base + (mine - orig) + (mine - orig) // 4 + 0x2000)
    return clamp32(mine + mine // 4 + 0x20000)

def files(root):
    for d, _, names in os.walk(root):
        for n in names:
            if not n.startswith('.'): yield os.path.relpath(os.path.join(d, n), root).replace(os.sep, '/')

def is_table(rel): return rel.startswith(TABLE_DIR + '/') and rel.endswith('.rsizetable.zs')

def skins(mods):
    root = os.path.join(mods, 'link-skins', 'skins')
    for sid in sorted(os.listdir(root)) if os.path.isdir(root) else []:
        if sid[0] in '._' or not os.path.isdir(os.path.join(root, sid, 'romfs')): continue
        meta = {}
        try: meta = json.load(open(os.path.join(root, sid, 'skin.json'), encoding='utf-8-sig'))
        except (OSError, ValueError): pass
        yield sid, os.path.join(root, sid, 'romfs'), meta

def main():
    if len(sys.argv) not in (2, 3): sys.exit(__doc__)
    game = sys.argv[1]
    mods = sys.argv[2] if len(sys.argv) == 3 else os.path.expanduser('~/Library/Application Support/Slingshot/mods')
    names = [n for n in os.listdir(os.path.join(game, TABLE_DIR)) if n.startswith('ResourceSizeTable.') and n.endswith('.rsizetable.zs')]
    if not names: sys.exit('no ResourceSizeTable.*.rsizetable.zs in %s/%s' % (game, TABLE_DIR))
    name = sorted(names)[0]; t = table_read(os.path.join(game, TABLE_DIR, name))
    for m in sorted(os.listdir(mods)) if os.path.isdir(mods) else []:   # the other mods' tables: their larger entries are kept
        r = os.path.join(mods, m, 'romfs', TABLE_DIR)
        if m == 'link-skins' or not os.path.isdir(r): continue
        for n in os.listdir(r):
            if n.endswith('.rsizetable.zs'): table_merge(t, table_read(os.path.join(r, n))); print('size table: the larger entries of mods/%s are kept' % m)
    count = 0
    for sid, root, meta in skins(mods):
        rels = sorted(files(root)); own = next((table_read(os.path.join(root, r)) for r in rels if is_table(r)), None)
        sizes = meta.get('sizes') or {}
        for rel in rels:
            if is_table(rel): continue
            mine = content_size(os.path.join(root, rel))
            if not mine: print('skin %s: %s does not say its unpacked size: left out' % (sid, rel)); continue
            key = rel[:-3] if rel.endswith('.zs') else rel
            gp = os.path.join(game, rel); orig = content_size(gp) if os.path.isfile(gp) else 0
            want = needed_size(key, mine, table_get(t, key), orig)
            if own: want = max(want, table_get(own, key))
            if isinstance(sizes.get(key), int): want = max(want, sizes[key])
            table_raise(t, key, want)
        count += 1; print('skin %s (%s): %d files' % (sid, meta.get('name', sid), sum(1 for r in rels if not is_table(r))))
    out = os.path.join(mods, 'link-skins', 'table', 'romfs', TABLE_DIR, name)
    if not count: print('no skins in %s/link-skins/skins: no size table written' % mods); return
    table_write(t, out); print('size table over %d skins: %s' % (count, out))

if __name__ == '__main__':
    main()
