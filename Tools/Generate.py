"""Writes Generated/ from Assets/ with other tools: Pillow (images), ffmpeg (sounds) and Blender (EXR, HDR and the
other formats it writes, models through its importers and exporters, see BlenderGenerate.py).

    python -I Tools/Generate.py [--blender <blender.exe>] [--ffmpeg <ffmpeg.exe>]

Every run deletes Generated/ and writes it again; the files are small on purpose (the corpus stays in git).
"""

import argparse
import glob
import os
import shutil
import subprocess
import sys

from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ASSETS = os.path.join(ROOT, "Assets")
OUT = os.path.join(ROOT, "Generated")

DEFAULT_BLENDER = r"C:\Program Files\Blender Foundation\Blender 5.2\blender.exe"


def default_ffmpeg():
    found = glob.glob(os.path.expandvars(r"%LOCALAPPDATA%\Microsoft\WinGet\Packages\Gyan.FFmpeg*\ffmpeg-*\bin\ffmpeg.exe"))
    return found[0] if found else "ffmpeg"


def path(*parts):
    p = os.path.join(OUT, *parts)
    os.makedirs(os.path.dirname(p), exist_ok=True)
    return p


# ---------------------------------------------------------------------------------------------------- images

def images():
    photo = Image.open(os.path.join(ASSETS, "Models", "PolyHaven", "wooden_crate_02", "glTF", "textures", "wooden_crate_02_diff_1k.jpg"))
    # Odd sizes: partial JPEG blocks, BMP row padding, odd TIFF strips.
    rgb = photo.convert("RGB").resize((189, 141), Image.LANCZOS)
    mask = Image.open(os.path.join(ASSETS, "Models", "PolyHaven", "wooden_crate_02", "PNG", "wooden_crate_02_mask_1k.png")).convert("L").resize(rgb.size)
    ramp = Image.linear_gradient("L").resize(rgb.size)
    alpha = Image.eval(Image.blend(mask, ramp, 0.5), lambda v: v)
    rgba = rgb.copy()
    rgba.putalpha(alpha)
    gray = rgb.convert("L")
    pal = rgb.quantize(colors=200)
    pal16 = rgb.quantize(colors=16)
    bw = gray.point(lambda v: 255 if v > 110 else 0).convert("1")
    g16 = gray.convert("I").point(lambda v: v * 257).convert("I;16")
    pal_t = rgba.quantize(colors=255)
    pal_t.info["transparency"] = 0

    d = ["Images", "Pillow"]
    save = lambda img, name, **kw: img.save(path(*d, name), **kw)

    save(rgb, "png_rgb8.png")
    save(rgba, "png_rgba8.png")
    save(gray, "png_gray8.png")
    save(gray.convert("LA"), "png_gray_alpha8.png")
    save(pal, "png_palette8.png")
    save(pal16, "png_palette4.png", bits=4)
    save(pal_t, "png_palette_trns.png")
    save(bw, "png_gray1.png")
    save(g16, "png_gray16.png")
    save(rgb, "png_rgb8_level9.png", compress_level=9)
    save(rgb, "png_rgb8_level0.png", compress_level=0)

    save(rgb, "jpeg_444_q90.jpg", quality=90, subsampling=0)
    save(rgb, "jpeg_422_q85.jpg", quality=85, subsampling=1)
    save(rgb, "jpeg_420_q75.jpg", quality=75, subsampling=2)
    save(rgb, "jpeg_420_q20.jpg", quality=20, subsampling=2)
    save(rgb, "jpeg_progressive.jpg", quality=85, progressive=True)
    save(rgb, "jpeg_optimized.jpg", quality=85, optimize=True)
    save(rgb, "jpeg_restart.jpg", quality=85, restart_marker_rows=1)
    save(gray, "jpeg_gray.jpg", quality=85)
    save(rgb.convert("CMYK"), "jpeg_cmyk.jpg", quality=90)

    save(bw, "bmp_1.bmp")
    save(pal, "bmp_8_palette.bmp")
    save(gray, "bmp_8_gray.bmp")
    save(rgb, "bmp_24.bmp")
    save(rgba, "bmp_32.bmp")

    save(rgb, "tga_24.tga")
    save(rgba, "tga_32.tga")
    save(gray, "tga_8_gray.tga")
    save(pal, "tga_8_palette.tga")
    save(rgb, "tga_24_rle.tga", rle=True)
    save(rgba, "tga_32_rle.tga", rle=True)
    save(rgba, "tga_32_top_left.tga", orientation=1)

    save(pal, "gif_static.gif")
    save(pal_t, "gif_transparent.gif", transparency=0)
    save(pal, "gif_interlaced.gif", interlace=True)
    frames = []
    for i in range(4):
        f = rgb.rotate(i * 20, fillcolor=(30 * i, 0, 0)).quantize(colors=64)
        frames.append(f)
    frames[0].save(path(*d, "gif_animated.gif"), save_all=True, append_images=frames[1:], duration=[40, 80, 120, 160], loop=0,
                   disposal=[0, 1, 2, 3])

    for comp in ["raw", "tiff_lzw", "tiff_adobe_deflate", "packbits", "jpeg"]:
        save(rgb, "tiff_rgb_%s.tif" % comp.replace("tiff_", ""), compression=comp)
    save(rgba, "tiff_rgba_lzw.tif", compression="tiff_lzw")
    save(gray, "tiff_gray_lzw.tif", compression="tiff_lzw")
    save(g16, "tiff_gray16_deflate.tif", compression="tiff_adobe_deflate")
    save(bw, "tiff_bw_group4.tif", compression="group4")
    save(rgb.convert("CMYK"), "tiff_cmyk_lzw.tif", compression="tiff_lzw")
    save(pal, "tiff_palette.tif", compression="tiff_lzw")

    # The block formats on sizes of whole blocks.
    block = rgba.resize((128, 96))
    for fmt in ["DXT1", "DXT3", "DXT5"]:
        try:
            block.save(path(*d, "dds_%s.dds" % fmt.lower()), pixel_format=fmt)
        except Exception as e:
            print("dds %s: %s" % (fmt, e))
    block.save(path(*d, "dds_rgba.dds"))
    block.convert("RGB").save(path(*d, "dds_rgb.dds"))
    block.convert("L").save(path(*d, "dds_gray.dds"))


# ---------------------------------------------------------------------------------------------------- sounds

def sounds(ffmpeg):
    d = ["Audio", "ffmpeg"]
    # A chord on the left, a tone with noise on the right; short so the references stay small.
    source = "aevalsrc=exprs='0.4*sin(2*PI*440*t)+0.2*sin(2*PI*660*t+1)|0.3*sin(2*PI*554*t)+0.05*(random(0)-0.5)':s=48000:d=0.25"
    surround = "aevalsrc=exprs='0.3*sin(2*PI*220*t)|0.3*sin(2*PI*330*t)|0.3*sin(2*PI*440*t)|0.2*sin(2*PI*55*t)|0.3*sin(2*PI*550*t)|0.3*sin(2*PI*660*t)':s=48000:d=0.25:c=5.1"

    def run(name, args, src=source):
        out = path(*d, name)
        cmd = [ffmpeg, "-hide_banner", "-loglevel", "error", "-y", "-f", "lavfi", "-i", src] + args + ["-metadata", "title=Helix corpus", out]
        subprocess.run(cmd, check=True)

    for codec, name in [("pcm_u8", "wav_u8"), ("pcm_s16le", "wav_s16"), ("pcm_s24le", "wav_s24"), ("pcm_s32le", "wav_s32"),
                        ("pcm_f32le", "wav_f32"), ("pcm_f64le", "wav_f64"), ("pcm_alaw", "wav_alaw"), ("pcm_mulaw", "wav_mulaw"),
                        ("adpcm_ima_wav", "wav_ima_adpcm"), ("adpcm_ms", "wav_ms_adpcm")]:
        run(name + ".wav", ["-c:a", codec])
    run("wav_s16_mono_8k.wav", ["-ac", "1", "-ar", "8000", "-c:a", "pcm_s16le"])
    run("wav_s16_22k.wav", ["-ar", "22050", "-c:a", "pcm_s16le"])
    run("wav_s24_96k.wav", ["-ar", "96000", "-c:a", "pcm_s24le"])
    run("wav_s16_51.wav", ["-c:a", "pcm_s16le"], surround)
    run("wav_rf64.wav", ["-c:a", "pcm_s16le", "-rf64", "always"])
    run("w64_s16.w64", ["-c:a", "pcm_s16le"])

    run("flac_16.flac", ["-c:a", "flac", "-sample_fmt", "s16"])
    run("flac_24.flac", ["-c:a", "flac", "-sample_fmt", "s32", "-bits_per_raw_sample", "24"])
    run("flac_16_level0.flac", ["-c:a", "flac", "-sample_fmt", "s16", "-compression_level", "0"])
    run("flac_16_level12.flac", ["-c:a", "flac", "-sample_fmt", "s16", "-compression_level", "12"])
    run("flac_16_mono_22k.flac", ["-c:a", "flac", "-sample_fmt", "s16", "-ac", "1", "-ar", "22050"])
    run("flac_16_51.flac", ["-c:a", "flac", "-sample_fmt", "s16"], surround)

    run("mp3_cbr128.mp3", ["-c:a", "libmp3lame", "-b:a", "128k"])
    run("mp3_cbr320.mp3", ["-c:a", "libmp3lame", "-b:a", "320k"])
    run("mp3_vbr2.mp3", ["-c:a", "libmp3lame", "-q:a", "2"])
    run("mp3_mono_22k.mp3", ["-c:a", "libmp3lame", "-ac", "1", "-ar", "22050", "-b:a", "48k"])
    run("mp3_8k.mp3", ["-c:a", "libmp3lame", "-ac", "1", "-ar", "8000", "-b:a", "16k"])
    run("mp3_no_tags.mp3", ["-c:a", "libmp3lame", "-b:a", "96k", "-write_xing", "0", "-id3v2_version", "0"])

    run("vorbis_q0.ogg", ["-c:a", "libvorbis", "-q:a", "0"])
    run("vorbis_q5.ogg", ["-c:a", "libvorbis", "-q:a", "5"])
    run("vorbis_q10.ogg", ["-c:a", "libvorbis", "-q:a", "10"])
    run("vorbis_mono_22k.ogg", ["-c:a", "libvorbis", "-ac", "1", "-ar", "22050", "-q:a", "3"])
    run("vorbis_51.ogg", ["-c:a", "libvorbis", "-q:a", "4"], surround)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--blender", default=DEFAULT_BLENDER)
    parser.add_argument("--ffmpeg", default=default_ffmpeg())
    args = parser.parse_args()
    if os.path.isdir(OUT):
        shutil.rmtree(OUT)
    os.makedirs(OUT)
    images()
    sounds(args.ffmpeg)
    script = os.path.join(ROOT, "Tools", "BlenderGenerate.py")
    subprocess.run([args.blender, "--background", "--factory-startup", "--python-exit-code", "1", "--python", script, "--", ROOT], check=True)
    count = sum(len(files) for _, _, files in os.walk(OUT))
    print("%d generated files" % count)


if __name__ == "__main__":
    sys.exit(main())
