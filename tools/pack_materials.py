#!/usr/bin/env python3
"""Packs doom64-rt's Retribution-RT-Materials folder into one pk3 for releases.

Usage: tools/pack_materials.py <Retribution-RT-Materials dir> <out.pk3>
Takes rt/mat (material maps) and rt/data/*.json (defaults, light colours); skips the
_dev and quarantine copies and per-scene data. PNGs are stored (already compressed).
"""
import os
import sys
import zipfile

src, out = sys.argv[1], sys.argv[2]
n = 0
with zipfile.ZipFile(out, "w") as z:
    for root, dirs, files in os.walk(src):
        rel = os.path.relpath(root, src).replace(os.sep, "/")
        if "quarantine" in rel or rel.endswith("_dev") or "_dev/" in rel or "/scenes" in rel:
            continue
        if not (rel.startswith("rt/mat") or rel == "rt/data"):
            continue
        for f in sorted(files):
            if not f.lower().endswith((".png", ".json")):
                continue
            method = zipfile.ZIP_STORED if f.lower().endswith(".png") else zipfile.ZIP_DEFLATED
            z.write(os.path.join(root, f), rel + "/" + f, compress_type=method)
            n += 1
print(f"{out}: {n} files")
