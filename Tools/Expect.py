"""Writes Expected/: what other tools read from every corpus file, for the corpus test to compare with.

    python -I Tools/Expect.py [--blender <blender.exe>] [--ffmpeg <ffmpeg.exe>]

- Images: Pillow's pixels (a SHA-256 of RGBA8, or of the 16 bit samples), reference PNGs of the small generated files,
  8x8 block means of large JPEGs; 16 bit color PNGs through the small PNG reader below (Pillow keeps 8 bits of them).
- Float images (EXR, HDR) and models: Blender, see BlenderExpect.py.
- Sounds: ffmpeg's samples as float WAV files.
- Fonts: FreeType through Pillow (advances at one pixel per unit, ink and size of each glyph at 32 pixels).
- JSON (glTF files and GLB chunks): Python's json, as counts, a sum of the numbers and a hash of keys and strings.
"""

import argparse
import glob
import hashlib
import json
import os
import shutil
import struct
import subprocess
import sys
import zlib

from PIL import Image, ImageFont

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from Generate import DEFAULT_BLENDER, default_ffmpeg  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "Expected")

IMAGE_EXT = {".png", ".jpg", ".jpeg", ".bmp", ".tga", ".gif", ".tif", ".tiff", ".dds", ".psd"}
AUDIO_EXT = {".wav", ".w64", ".flac", ".mp3", ".ogg"}
FONT_EXT = {".otf", ".ttf", ".ttc"}


def rel(p):
    return os.path.relpath(p, ROOT).replace("\\", "/")


def corpus_files(exts):
    found = []
    for top in ("Assets", "Generated"):
        for dirpath, _, files in os.walk(os.path.join(ROOT, top)):
            for f in files:
                if os.path.splitext(f)[1].lower() in exts:
                    found.append(os.path.join(dirpath, f))
    return sorted(found, key=rel)


def expected(*parts):
    p = os.path.join(OUT, *parts)
    os.makedirs(os.path.dirname(p), exist_ok=True)
    return p


# ---------------------------------------------------------------------------------------------------- images

def png16(path):
    """RGBA16 samples (little endian) of a non interlaced 16 bit PNG, or None for other PNGs."""
    data = open(path, "rb").read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        return None
    pos, idat, ihdr = 8, b"", None
    while pos < len(data):
        length, kind = struct.unpack(">I4s", data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + length]
        if kind == b"IHDR":
            ihdr = struct.unpack(">IIBBBBB", body)
        elif kind == b"IDAT":
            idat += body
        pos += 12 + length
    w, h, depth, color, _, _, interlace = ihdr
    channels = {0: 1, 2: 3, 4: 2, 6: 4}.get(color)
    if depth != 16 or interlace or channels is None:
        return None
    bpp = channels * 2
    stride = w * bpp
    raw = zlib.decompress(idat)
    rows, prev = [], bytearray(stride)
    for y in range(h):
        f = raw[y * (stride + 1)]
        line = bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for i in range(stride):
            a = line[i - bpp] if i >= bpp else 0
            b = prev[i]
            c = prev[i - bpp] if i >= bpp else 0
            if f == 1:
                line[i] = (line[i] + a) & 255
            elif f == 2:
                line[i] = (line[i] + b) & 255
            elif f == 3:
                line[i] = (line[i] + (a + b) // 2) & 255
            elif f == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        rows.append(bytes(line))
        prev = line
    out = bytearray()
    for line in rows:
        for x in range(w):
            s = [struct.unpack(">H", line[x * bpp + 2 * k:x * bpp + 2 * k + 2])[0] for k in range(channels)]
            if channels == 1:
                s = [s[0], s[0], s[0], 65535]
            elif channels == 2:
                s = [s[0], s[0], s[0], s[1]]
            elif channels == 3:
                s = s + [65535]
            out += struct.pack("<4H", *s)
    return w, h, bytes(out)


def block_means(img):
    w, h = img.size
    bw, bh = (w + 7) // 8, (h + 7) // 8
    return img.convert("RGB").resize((bw, bh), Image.BOX)


def images():
    entries = []
    for p in corpus_files(IMAGE_EXT):
        r = rel(p)
        e = {"file": r}
        sixteen = png16(p) if p.lower().endswith(".png") else None
        if sixteen:
            e.update(width=sixteen[0], height=sixteen[1], frames=1, pixels="rgba16", sha256=hashlib.sha256(sixteen[2]).hexdigest())
            entries.append(e)
            continue
        try:
            im = Image.open(p)
            im.load()
        except Exception as ex:
            e.update(pillow="cannot read: %s" % ex)
            entries.append(e)
            continue
        e.update(width=im.size[0], height=im.size[1], frames=getattr(im, "n_frames", 1), mode=im.mode)
        lossy = im.format == "JPEG" or (im.format == "TIFF" and im.info.get("compression") == "jpeg") or im.format == "DDS"
        e["lossy"] = lossy
        digests = []
        for f in range(e["frames"]):
            im.seek(f)
            rgba = im.convert("RGBA")
            digests.append(hashlib.sha256(rgba.tobytes()).hexdigest())
            small = r.startswith("Generated/") and rgba.size[0] * rgba.size[1] <= 256 * 256
            if small:
                ref = "Expected/Images/%s.%d.png" % (r, f)
                rgba.save(expected(*ref.split("/")[1:]))
                e.setdefault("references", []).append(ref)
        if im.mode == "I;16":
            im.seek(0)
            e["gray16_sha256"] = hashlib.sha256(im.tobytes("raw", "I;16")).hexdigest()
        e["sha256"] = digests
        if lossy and "references" not in e:
            im.seek(0)
            ref = "Expected/Images/%s.blocks.png" % r
            block_means(im).save(expected(*ref.split("/")[1:]))
            e["blocks"] = ref
        entries.append(e)
    return entries


# ---------------------------------------------------------------------------------------------------- sounds

def sounds(ffmpeg, ffprobe):
    entries = []
    for p in corpus_files(AUDIO_EXT):
        r = rel(p)
        ref = "Expected/Audio/%s.wav" % r
        subprocess.run([ffmpeg, "-hide_banner", "-loglevel", "error", "-y", "-i", p, "-c:a", "pcm_f32le", "-f", "wav", expected(*ref.split("/")[1:])],
                       check=True)
        info = json.loads(subprocess.run([ffprobe, "-v", "error", "-show_streams", "-of", "json", p], check=True, capture_output=True).stdout)
        s = info["streams"][0]
        lossy = s["codec_name"] in ("mp3", "vorbis")
        entries.append({"file": r, "reference": ref, "rate": int(s["sample_rate"]), "channels": int(s["channels"]), "codec": s["codec_name"],
                        "lossy": lossy})
    return entries


# ---------------------------------------------------------------------------------------------------- fonts

def tables(data):
    n = struct.unpack(">H", data[4:6])[0]
    t = {}
    for i in range(n):
        tag, _, off, length = struct.unpack(">4sIII", data[12 + 16 * i:28 + 16 * i])
        t[tag.decode("latin1")] = data[off:off + length]
    return t


def cmap_codes(cmap):
    """Code points of the format 4 / 12 subtables (Unicode or Windows Unicode)."""
    n = struct.unpack(">H", cmap[2:4])[0]
    codes = set()
    for i in range(n):
        pid, eid, off = struct.unpack(">HHI", cmap[4 + 8 * i:12 + 8 * i])
        if not (pid == 0 or (pid == 3 and eid in (1, 10))):
            continue
        fmt = struct.unpack(">H", cmap[off:off + 2])[0]
        if fmt == 4:
            segs = struct.unpack(">H", cmap[off + 6:off + 8])[0] // 2
            ends = struct.unpack(">%dH" % segs, cmap[off + 14:off + 14 + 2 * segs])
            starts = struct.unpack(">%dH" % segs, cmap[off + 16 + 2 * segs:off + 16 + 4 * segs])
            for s, e in zip(starts, ends):
                if s != 0xFFFF:
                    codes.update(range(s, e + 1))
        elif fmt == 12:
            groups = struct.unpack(">I", cmap[off + 12:off + 16])[0]
            for g in range(groups):
                s, e, _ = struct.unpack(">III", cmap[off + 16 + 12 * g:off + 28 + 12 * g])
                codes.update(range(s, e + 1))
    return codes


def fonts():
    entries = []
    chars = [chr(c) for c in range(33, 127)] + list(map(chr, (0xC4, 0xE9, 0xDF, 0x141, 0x20AC)))
    for p in corpus_files(FONT_EXT):
        data = open(p, "rb").read()
        t = tables(data)
        upem = struct.unpack(">H", t["head"][18:20])[0]
        codes = cmap_codes(t["cmap"])
        units = ImageFont.truetype(p, size=upem, layout_engine=ImageFont.Layout.BASIC)
        px = ImageFont.truetype(p, size=32, layout_engine=ImageFont.Layout.BASIC)
        family, style = units.getname()
        glyphs = []
        for ch in chars:
            if ord(ch) not in codes:
                continue
            m = px.getmask(ch, mode="L")
            glyphs.append({"code": ord(ch), "advance": int(round(units.getlength(ch))), "ink": sum(m), "width": m.size[0], "height": m.size[1]})
        entries.append({"file": rel(p), "family": family, "style": style, "upem": upem, "codes": len(codes), "glyphs": glyphs})
    return entries


# ---------------------------------------------------------------------------------------------------- json

def fnv(h, b):
    for c in b:
        h = ((h ^ c) * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return h


def json_stats(doc):
    s = {"objects": 0, "arrays": 0, "strings": 0, "numbers": 0, "bools": 0, "nulls": 0, "sum": 0.0, "depth": 0}
    h = [0xCBF29CE484222325]

    def walk(v, depth):
        s["depth"] = max(s["depth"], depth)
        if isinstance(v, dict):
            s["objects"] += 1
            for k, x in v.items():
                h[0] = fnv(h[0], k.encode("utf-8") + b"\0")
                walk(x, depth + 1)
        elif isinstance(v, list):
            s["arrays"] += 1
            for x in v:
                walk(x, depth + 1)
        elif isinstance(v, str):
            s["strings"] += 1
            h[0] = fnv(h[0], v.encode("utf-8") + b"\1")
        elif isinstance(v, bool):
            s["bools"] += 1
        elif v is None:
            s["nulls"] += 1
        else:
            s["numbers"] += 1
            s["sum"] += float(v)

    walk(doc, 1)
    s["hash"] = "%016x" % h[0]
    return s


def jsons():
    entries = []
    for p in corpus_files({".gltf", ".glb"}):
        data = open(p, "rb").read()
        if p.lower().endswith(".glb"):
            length, kind = struct.unpack("<II", data[12:20])
            text = data[20:20 + length]
        else:
            text = data
        e = {"file": rel(p)}
        e.update(json_stats(json.loads(text.decode("utf-8"))))
        entries.append(e)
    return entries


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--blender", default=DEFAULT_BLENDER)
    parser.add_argument("--ffmpeg", default=default_ffmpeg())
    args = parser.parse_args()
    ffprobe = os.path.join(os.path.dirname(args.ffmpeg), "ffprobe.exe")
    if os.path.isdir(OUT):
        shutil.rmtree(OUT)
    os.makedirs(OUT)
    manifest = {"images": images(), "audio": sounds(args.ffmpeg, ffprobe), "fonts": fonts(), "json": jsons()}
    blender_out = os.path.join(OUT, "blender.json")
    script = os.path.join(ROOT, "Tools", "BlenderExpect.py")
    subprocess.run([args.blender, "--background", "--factory-startup", "--python-exit-code", "1", "--python", script, "--", ROOT, blender_out],
                   check=True, stdout=subprocess.DEVNULL)
    manifest.update(json.load(open(blender_out)))
    os.remove(blender_out)
    with open(os.path.join(OUT, "Manifest.json"), "w", encoding="utf-8", newline="\n") as f:
        json.dump(manifest, f, indent=1, ensure_ascii=True)
        f.write("\n")
    print(", ".join("%d %s" % (len(v), k) for k, v in manifest.items()))


if __name__ == "__main__":
    sys.exit(main())
