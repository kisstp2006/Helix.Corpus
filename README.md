# Helix.Corpus

Real files for the format libraries of the Helix engine (Helix.Image, Helix.Serialization, Helix.Font, Helix.Audio,
Helix.Model): public domain (CC0) images, fonts and models, files other tools wrote from them, and what those tools
read from every file. The tests read each file with the libraries and compare; the fuzz targets mutate each file.

## Contents

| Path | Content |
|---|---|
| `Assets/` | Downloaded files, all CC0: Khronos glTF sample models whose every part is CC0, Poly Haven (an HDRI as HDR and EXR, a model as FBX and glTF with its textures), Typodermic OpenType fonts |
| `Generated/` | Files of other tools made from `Assets/`: Pillow (PNG, JPEG, BMP, TGA, GIF, TIFF and DDS variants), Blender (EXR in every codec it writes, HDR, TIFF, PNG, TGA, BMP, JPEG; models through its importers to FBX, OBJ, GLB and glTF), ffmpeg (WAV of every sample format, W64, RF64, FLAC, MP3, Vorbis) |
| `Expected/Manifest.json` | What the other tools read from each file, and the reference pixels and samples it points to |
| `Tests/Corpus/` | `Images`, `FloatImages`, `Audio`, `Fonts`, `Json`, `Models`, `TangentsAgainstTheFiles`, `Encoders` |
| `Fuzz/CorpusFuzz.cpp` | `CorpusImages`, `CorpusSounds`, `CorpusFonts`, `CorpusModels`, `CorpusJson`: every corpus file a seed |
| `Tools/` | `Download.py`, `Generate.py` (+ `BlenderGenerate.py`), `Expect.py` (+ `BlenderExpect.py`), `CheckEncoded.py`, `Sources.json` |
| `THIRD-PARTY.txt` | The sources and licenses of `Assets/` |

## What is compared

- Images (Pillow): lossless files exactly, by a SHA-256 of their RGBA pixels (16 bit PNGs by their samples, read by a
  small reader in `Expect.py` because Pillow keeps 8 bits); JPEG and block compressed DDS against Pillow's pixels
  (PSNR at least 40 dB); every GIF frame; colors of fully transparent pixels left out.
- EXR and HDR (Blender): half and float values exactly at 128 samples; RGBE within half a mantissa step (Blender takes
  the mantissa as stored, the decoder the middle of its step as Radiance's reader does); every lossless EXR codec
  gives the pixels of the uncompressed file.
- Sounds (ffmpeg): PCM, A-law, mu-law, ADPCM and FLAC exactly; MP3 and Vorbis at an SNR of at least 60 dB after
  aligning the encoder delay.
- Fonts (FreeType through Pillow): names, units per em, the mapping and advance of the ASCII and some Latin
  characters exactly, the ink of each glyph at 32 pixels within 12 % (FreeType hints CFF stems); every glyph of every
  font outlined, rasterized and packed, the layout tables read.
- JSON (Python): every glTF document and GLB chunk gives the same counts, number sum and hash of keys and strings,
  and comes back the same after our writer.
- Models (Blender): triangles, cameras and lights (and meshes and shape keys for glTF); for files without animation
  the world bounds after the conversion to the engine frame; then welding, normals, tangents, the three reorderings,
  meshlets, simplification and blend shape compression on every primitive with their invariants.
- Tangents: against Blender's own MikkTSpace exports every corner within 0.8 degrees with the same sign; against
  other files' tangents within 8 degrees, with one sign convention per file; files whose tangents leave the normals'
  plane are not MikkTSpace and are skipped.
- Encoders: PNG, TGA and every EXR codec give the pixels back exactly, JPG within its quality; `CheckEncoded.py`
  reads the files with Pillow (JPG against libjpeg at the same quality) and Blender.

Files a library does not read on purpose (EXR PXR24, DWAA, DWAB and HTJ2K, TIFF CCITT, JPEG and CMYK, glTF files
that require Draco, meshopt or KHR_node_visibility) are listed in the tests and must be refused with an error.

## Making it again

```
python -I Tools/Download.py --staging <new empty folder>
python -I Tools/Generate.py
python -I Tools/Expect.py
```

`Download.py` checks every file against the SHA-256 in `Sources.json`. `Generate.py` and `Expect.py` need Pillow,
ffmpeg and Blender (the default paths are the Windows ones; `--blender` and `--ffmpeg` choose others). After the
tests ran, `python -I Tools/CheckEncoded.py <build>/Encoded` reads what the encoders wrote.

## Building

The repository builds with the presets of Helix.CMake (Windows, Web, Android); the tests read the corpus from this
folder (Node.js reads it from the disk on the Web). The submodules in `External/` are Helix.CMake, Helix.Testing,
Helix.Core, Helix.Platform, Helix.Profiler, Helix.Jobs, Helix.Image, Helix.Serialization, Helix.Font, Helix.Audio and
Helix.Model.

## License

The code is GPL-3.0, see `LICENSE`. The files in `Assets/` are CC0 1.0 (see `THIRD-PARTY.txt`); `Generated/` and
`Expected/` are made from them by the tools and are CC0 1.0 as well.
