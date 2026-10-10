#!/usr/bin/env python3
"""Build the single release PK3 from compatible Retribution art (no game ROM needed)."""
import argparse
import json
from pathlib import Path
import struct
import zipfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('output', type=Path)
args = parser.parse_args()
root = Path(__file__).resolve().parent.parent
source = root / 'addons/doom64-retribution'
entries = {}
origins = {}

def add(name, data, origin):
    if name in entries:
        raise ValueError(f'duplicate pack path: {name}')
    entries[name] = data
    origins[name] = origin

for pack in ('d64r-lostsoul-rt.pk3', 'd64r-caco-ball-recolor.pk3', 'd64r-mugshot.pk3','d64r-lava-fx.pk3','d64r-poison-fx.pk3'):
    with zipfile.ZipFile(source / pack) as archive:
        for name in sorted(archive.namelist()):
            if (name.startswith('sprites/') or name.startswith('mugface/')) and name.endswith('.png'):
                add(name, archive.read(name), pack)

# These PNG lumps match original N64 ceiling-light texture names. Do not include
# d64r-sflatas-broken.wad or GZDoom map/script replacements.
wad = (source / 'd64r-bulb-textures.wad').read_bytes()
count, directory = struct.unpack_from('<II', wad, 4)
for index in range(count):
    offset, size, raw_name = struct.unpack_from('<II8s', wad, directory + 16 * index)
    name = raw_name.rstrip(b'\0').decode('ascii')
    if name in ('SFLATC', 'SPACECE'):
        png = wad[offset:offset + size]
        if not png.startswith(b'\x89PNG\r\n\x1a\n'):
            raise ValueError(f'{name} is not a PNG texture')
        add(f'textures/{name}.png', png, 'd64r-bulb-textures.wad')

materials = source / 'Retribution-RT-Materials'
for path in sorted((materials / 'rt/mat').glob('*.png')):
    if path.stem.endswith(('_orm', '_n', '_e')):
        add(path.relative_to(materials).as_posix(), path.read_bytes(), 'Retribution-RT-Materials')
for path in sorted((materials / 'rt/data').glob('*.json')):
    add(path.relative_to(materials).as_posix(), path.read_bytes(), 'Retribution-RT-Materials')
add('CREDITS.txt', (root / 'tools/release/PACKS_CREDITS.txt').read_bytes(), 'Doom64-RTX')
# Native adapters inspect selected declarations. They do not execute scripts.
for pack in ('d64r-blood-persist.pk3','d64r-rt-flashlight.pk3','d64r-mugshot.pk3','d64r-lava-fx.pk3','d64r-poison-fx.pk3'):
    with zipfile.ZipFile(source / pack) as archive:
        for name in ('DECORATE','ZSCRIPT','SBARINFO'):
            if name in archive.namelist():
                add('native/' + pack[:-4] + '/' + name,archive.read(name),pack)
for path in sorted((root / 'assets/combat/kick').glob('*.png')):
    add('fx/kick/' + path.name, path.read_bytes(), 'BrutalDoomPlatinum 423b716')
for path in sorted((root / 'assets/sounds').glob('*.wav')):
    add('sounds/immersion/' + path.name, path.read_bytes(), 'BrutalDoomPlatinum 423b716')
if (root / 'assets/sounds/SOURCES.txt').exists():
    for name in ('SOURCES.txt','LICENSE.BDP','DetailedCredits.txt'):
        add('credits/brutaldoom/' + name,(root / 'assets/sounds' / name).read_bytes(),'BrutalDoomPlatinum 423b716')
add('manifest.json', json.dumps({'format': 1, 'sources': origins}, indent=2).encode(), 'Doom64-RTX')
args.output.parent.mkdir(parents=True, exist_ok=True)
with zipfile.ZipFile(args.output, 'w') as archive:
    for name, data in sorted(entries.items()):
        info = zipfile.ZipInfo(name, (2026, 1, 1, 0, 0, 0))
        info.compress_type = zipfile.ZIP_STORED if name.endswith('.png') else zipfile.ZIP_DEFLATED
        archive.writestr(info, data)
print(f'{args.output}: {len(entries)} entries, {args.output.stat().st_size} bytes')
