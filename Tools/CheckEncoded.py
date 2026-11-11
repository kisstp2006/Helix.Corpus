"""Reads the files our encoders wrote (Corpus.Encoders writes them to Encoded/ in its working directory) with other
tools: Pillow for PNG, TGA and JPG (JPG against libjpeg's own file of the same quality), Blender for EXR.

    python -I Tools/CheckEncoded.py <Encoded folder> [--blender <blender.exe>]

Prints one line per file and exits with 1 when a file is not read or its pixels differ.
"""

import argparse
import json
import math
import os
import subprocess
import sys
import tempfile

from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from Generate import DEFAULT_BLENDER  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

BLENDER_SCRIPT = r'''
import bpy, json, sys
args = sys.argv[sys.argv.index("--") + 1:]
out = {}
for path in args[1:]:
    img = bpy.data.images.load(path, check_existing=False)
    img.colorspace_settings.name = "Non-Color"
    px = [0.0] * (img.size[0] * img.size[1] * 4)
    img.pixels.foreach_get(px)
    out[path] = {"size": list(img.size), "pixels": px}
json.dump(out, open(args[0], "w"))
'''


def psnr(a, b):
    a, b = a.convert("RGB").tobytes(), b.convert("RGB").tobytes()
    se = sum((x - y) * (x - y) for x, y in zip(a, b))
    return 999.0 if se == 0 else 10 * math.log10(255.0 * 255.0 * len(a) / se)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("folder")
    parser.add_argument("--blender", default=DEFAULT_BLENDER)
    args = parser.parse_args()
    source = Image.open(os.path.join(ROOT, "Generated", "Images", "Pillow", "png_rgba8.png")).convert("RGBA")
    failed = 0
    exrs = []
    for name in sorted(os.listdir(args.folder)):
        path = os.path.abspath(os.path.join(args.folder, name))
        ext = os.path.splitext(name)[1].lower()
        if ext == ".exr":
            exrs.append(path)
            continue
        try:
            im = Image.open(path)
            im.load()
        except Exception as e:
            print("%s: Pillow cannot read it: %s" % (name, e))
            failed += 1
            continue
        if ext in (".png", ".tga"):
            same = im.convert("RGBA").tobytes() == source.tobytes()
            print("%s: Pillow reads %s pixels" % (name, "the same" if same else "DIFFERENT"))
            failed += not same
        elif ext == ".jpg":
            quality = int(name.split("_q")[1].split(".")[0])
            ours = psnr(im, source)
            with tempfile.TemporaryDirectory() as tmp:
                theirs_path = os.path.join(tmp, "libjpeg.jpg")
                source.convert("RGB").save(theirs_path, quality=quality)
                theirs = psnr(Image.open(theirs_path), source)
            ok = ours >= theirs - 1.5
            print("%s: PSNR %.1f dB, libjpeg at quality %d %.1f dB%s" % (name, ours, quality, theirs, "" if ok else "  <- more than 1.5 dB worse"))
            failed += not ok
    if exrs:
        with tempfile.TemporaryDirectory() as tmp:
            script = os.path.join(tmp, "read.py")
            result = os.path.join(tmp, "out.json")
            open(script, "w").write(BLENDER_SCRIPT)
            reference = os.path.join(ROOT, "Generated", "Images", "Blender", "exr_none_float_rgba.exr")
            subprocess.run([args.blender, "--background", "--factory-startup", "--python-exit-code", "1", "--python", script, "--", result, reference] + exrs,
                           check=True, stdout=subprocess.DEVNULL)
            read = json.load(open(result))
        ref = read[reference]["pixels"]
        for path in exrs:
            px = read[path]["pixels"]
            name = os.path.basename(path)
            # Half files: the reference rounded to half (relative 2^-11); float files: exactly.
            tolerance = 0 if "_float" in name else 1.0 / 1024
            worst = max(abs(a - b) / (abs(b) + 1e-6) for a, b in zip(px, ref))
            ok = len(px) == len(ref) and worst <= tolerance
            print("%s: Blender reads it, largest relative difference %.3g%s" % (name, worst, "" if ok else "  <- outside"))
            failed += not ok
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
