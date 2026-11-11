#include <Support/Corpus.h>
#include <Support/Sha256.h>

#include <Helix/Image/Codecs/ImageDecoder.h>
#include <Helix/Image/Texture/BlockCodec.h>
#include <Helix/Image/Texture/Convert.h>
#include <Helix/Testing/Test.h>

#include <cmath>
#include <cstdio>
#include <cstring>

using namespace helix;
using namespace helix::corpus;
using namespace helix::image;

namespace
{
    // What the image decoders do not read (each must be refused with an error).
    constexpr Unsupported kUnsupported[] = {
        {"exr_pxr24_", "EXR PXR24 compression"},
        {"exr_dwaa_", "EXR DWAA compression"},
        {"exr_dwab_", "EXR DWAB compression"},
        {"exr_htj2k_", "EXR HTJ2K compression"},
        {"_nor_gl_1k.exr", "EXR DWAA compression (Poly Haven)"},
        {"tiff_bw_group4.tif", "TIFF CCITT group 4 compression"},
        {"tiff_rgb_jpeg.tif", "TIFF JPEG compression"},
        {"tiff_cmyk_lzw.tif", "TIFF CMYK (separated) color"},
    };

    const Unsupported* Skip(StringView file) { return FindUnsupported(file, kUnsupported, sizeof(kUnsupported) / sizeof(kUnsupported[0])); }

    // Files whose reading differs from Pillow's on purpose, compared in another way.
    constexpr Unsupported kRgbOnly[] = {
        {"bmp_32.bmp", "32 bit BI_RGB: Pillow leaves the fourth byte unused as the specification says, stb_image reads it as alpha "
                       "unless it is zero everywhere; the colors are compared"},
    };
    constexpr Unsupported kNotPillow[] = {
        {"Blender/tiff_rgba16_", "Pillow unpremultiplies 16 bit RGBA TIFFs at 8 bits; compared with Blender's 16 bit PNG of the same image"},
    };

    template <usize N>
    const Unsupported* In(StringView file, const Unsupported (&list)[N]) { return FindUnsupported(file, list, N); }

    // One layer as RGBA8 (block formats decompressed first).
    bool Rgba8(const TextureData& t, u32 layer, Array<u8>& out)
    {
        TextureData decoded;
        String error;
        const TextureData* src = &t;
        if (IsBlockFormat(t.format))
        {
            if (!DecompressTexture(t, decoded, error))
                return false;
            src = &decoded;
        }
        usize n = usize(src->width) * src->height;
        out.Resize(n * 4);
        return ConvertPixels(src->format, src->Pixels(layer), PixelFormat::RGBA8, out.Data(), n);
    }

    bool Rgba16(const TextureData& t, Array<u8>& out)
    {
        usize n = usize(t.width) * t.height;
        out.Resize(n * 8);
        return !IsBlockFormat(t.format) && ConvertPixels(t.format, t.Pixels(), PixelFormat::RGBA16, out.Data(), n);
    }

    bool Rgba32f(const TextureData& t, Array<float>& out)
    {
        usize n = usize(t.width) * t.height;
        out.Resize(n * 4);
        return !IsBlockFormat(t.format) && ConvertPixels(t.format, t.Pixels(), PixelFormat::RGBA32Float, reinterpret_cast<u8*>(out.Data()), n);
    }

    bool Decode(StringView file, TextureData& out, String& error)
    {
        Array<u8> bytes;
        if (!Read(file, bytes))
        {
            error = "cannot read the file";
            return false;
        }
        return DecodeImage(Span<const u8>(bytes), out, error, Extension(file));
    }

    struct Difference
    {
        u32 max = 0;
        double mean = 0, psnr = 999;
    };

    Difference Compare(const u8* a, const u8* b, usize bytes)
    {
        Difference d;
        double squares = 0;
        for (usize i = 0; i < bytes; i++)
        {
            u32 e = a[i] > b[i] ? a[i] - b[i] : b[i] - a[i];
            d.max = e > d.max ? e : d.max;
            d.mean += e;
            squares += double(e) * e;
        }
        d.mean /= double(bytes ? bytes : 1);
        double mse = squares / double(bytes ? bytes : 1);
        d.psnr = mse > 0 ? 10 * std::log10(255.0 * 255.0 / mse) : 999;
        return d;
    }

    // The 8 x 8 block means of an RGBA8 image, as RGB (what Expect.py keeps of large JPEGs).
    void BlockMeans(const u8* rgba, u32 w, u32 h, Array<u8>& out)
    {
        u32 bw = (w + 7) / 8, bh = (h + 7) / 8;
        out.Resize(usize(bw) * bh * 3);
        for (u32 by = 0; by < bh; by++)
            for (u32 bx = 0; bx < bw; bx++)
                for (u32 c = 0; c < 3; c++)
                {
                    double sum = 0;
                    u32 n = 0;
                    for (u32 y = by * 8; y < by * 8 + 8 && y < h; y++)
                        for (u32 x = bx * 8; x < bx * 8 + 8 && x < w; x++)
                            sum += rgba[(usize(y) * w + x) * 4 + c], n++;
                    out[(usize(by) * bw + bx) * 3 + c] = u8(sum / n + 0.5);
                }
    }

    void Report(StringView file, StringView what) { std::printf("    %s: %s\n", String(file).CStr(), String(what).CStr()); }
}

// Every image Pillow reads: the same pixels (lossless files exactly, by digest; JPEG and block formats against
// Pillow's pixels within bounds), the same size and frames.
HX_TEST(Corpus, Images)
{
    const Manifest& m = Manifest::Get();
    u32 list = m.Section("images");
    HX_REQUIRE(m.Count(list) > 50);
    u32 exact = 0, close = 0, refused = 0, pillowCannot = 0;
    for (u32 i = 0; i < m.Count(list); i++)
    {
        u32 e = m.Item(list, i);
        StringView file = m.Text(e, "file");
        TextureData t;
        String error;
        bool ok = Decode(file, t, error);
        if (const Unsupported* u = Skip(file))
        {
            if (ok)
                Report(file, Format("read, but listed as unsupported ({})", u->reason));
            HX_CHECK(!ok && !error.Empty());
            refused++;
            continue;
        }
        if (In(file, kNotPillow))
        {
            HX_CHECK(ok);
            continue;
        }
        if (m.Has(e, "pillow"))
        {
            // Pillow does not read it; ours must at least not fail without saying why.
            HX_CHECK(ok || !error.Empty());
            pillowCannot++;
            continue;
        }
        if (!ok)
        {
            Report(file, Format("not decoded: {}", error));
            HX_CHECK(ok);
            continue;
        }
        u32 w = u32(m.Number(e, "width")), h = u32(m.Number(e, "height"));
        if (t.width != w || t.height != h)
        {
            Report(file, Format("{} x {}, Pillow {} x {}", t.width, t.height, w, h));
            HX_CHECK(false);
            continue;
        }
        if (m.Text(e, "pixels") == StringView("rgba16"))
        {
            Array<u8> px;
            HX_CHECK(Rgba16(t, px));
            String digest = Sha256Hex(px.Data(), px.Size());
            bool same = digest == m.Text(e, "sha256");
            if (!same)
                Report(file, "16 bit pixels differ");
            HX_CHECK(same);
            exact += same;
            continue;
        }
        u32 frames = u32(m.Number(e, "frames", 1));
        if (t.layers != frames)
        {
            Report(file, Format("{} frames, Pillow {}", t.layers, frames));
            HX_CHECK(false);
            continue;
        }
        if (m.Has(e, "gray16_sha256"))
        {
            Array<u8> px, gray;
            HX_CHECK(Rgba16(t, px));
            for (usize p = 0; p < px.Size(); p += 8)
                gray.Push(px[p]), gray.Push(px[p + 1]);
            bool same = Sha256Hex(gray.Data(), gray.Size()) == m.Text(e, "gray16_sha256");
            if (!same)
                Report(file, "16 bit gray samples differ");
            HX_CHECK(same);
            // Pillow's RGBA of 16 bit gray clips at 255: the samples above are the comparison.
            exact += same;
            continue;
        }
        bool lossy = m.Flag(e, "lossy");
        u32 digests = m.Member(e, "sha256"), references = m.Member(e, "references");
        for (u32 f = 0; f < frames; f++)
        {
            Array<u8> ours;
            if (!Rgba8(t, f, ours))
            {
                Report(file, "cannot convert to RGBA8");
                HX_CHECK(false);
                break;
            }
            if (Sha256Hex(ours.Data(), ours.Size()) == m.Tree().Text(m.Item(digests, f)))
            {
                exact++;
                continue;
            }
            // Not the same bytes: compare with Pillow's pixels.
            TextureData ref;
            String refError;
            Difference d;
            if (references != serialization::json::kNoNode && Decode(m.Tree().Text(m.Item(references, f)), ref, refError))
            {
                Array<u8> theirs;
                Rgba8(ref, 0, theirs);
                bool rgbOnly = In(file, kRgbOnly) != nullptr;
                // The color of a pixel both see as fully transparent is not part of the image (GIF leaves it to the
                // reader); nor is alpha where the format has none by its specification.
                for (usize p = 0; p < ours.Size(); p += 4)
                    if (rgbOnly)
                        ours[p + 3] = theirs[p + 3];
                    else if (ours[p + 3] == 0 && theirs[p + 3] == 0)
                        for (u32 c = 0; c < 3; c++)
                            ours[p + c] = theirs[p + c];
                d = Compare(ours.Data(), theirs.Data(), ours.Size());
                if (d.max == 0)
                {
                    exact++;
                    continue;
                }
            }
            else if (m.Has(e, "blocks") && Decode(m.Text(e, "blocks"), ref, refError))
            {
                Array<u8> mine, theirs;
                BlockMeans(ours.Data(), t.width, t.height, mine);
                Rgba8(ref, 0, theirs);
                Array<u8> rgb;
                for (usize p = 0; p < theirs.Size(); p += 4)
                    rgb.Push(theirs[p]), rgb.Push(theirs[p + 1]), rgb.Push(theirs[p + 2]);
                d = Compare(mine.Data(), rgb.Data(), mine.Size());
            }
            else
            {
                Report(file, Format("frame {} differs and there is no reference", f));
                HX_CHECK(false);
                continue;
            }
            // Lossy decoders differ in rounding of their transforms; lossless files must match exactly.
            bool within = lossy && d.psnr >= 40 && d.mean <= 1.5;
            std::printf("    %s frame %u: max %u, mean %.3f, PSNR %.1f dB%s\n", String(file).CStr(), f, d.max, d.mean, d.psnr, within ? "" : "  <- outside");
            HX_CHECK(within);
            close += within;
        }
    }
    std::printf("    %u frames exact, %u within the lossy bounds, %u refused as listed, %u Pillow cannot read\n", exact, close, refused, pillowCannot);

    // Blender's 16 bit TIFFs carry the samples of its 16 bit PNG (checked against the PNG reader of Expect.py above).
    TextureData png;
    String error;
    Array<u8> pngPixels;
    HX_REQUIRE(Decode("Generated/Images/Blender/png_rgba16.png", png, error));
    HX_REQUIRE(Rgba16(png, pngPixels));
    for (const char* codec : {"none", "deflate", "lzw", "packbits"})
    {
        String file = Format("Generated/Images/Blender/tiff_rgba16_{}.tif", codec);
        TextureData tiff;
        Array<u8> tiffPixels;
        HX_REQUIRE(Decode(file, tiff, error));
        HX_REQUIRE(Rgba16(tiff, tiffPixels));
        // Blender writes associated alpha to TIFF, quantizing the premultiplied color its own way (its PNG path
        // rounds the straight color): one premultiplied step at alpha's scale apart at most.
        bool same = tiffPixels.Size() == pngPixels.Size();
        for (usize p = 0; same && p < pngPixels.Size(); p += 8)
        {
            u16 a[4], b[4];
            std::memcpy(a, tiffPixels.Data() + p, 8);
            std::memcpy(b, pngPixels.Data() + p, 8);
            same = a[3] == b[3];
            for (u32 c = 0; c < 3 && same && b[3]; c++)
                same = u32(a[c] > b[c] ? a[c] - b[c] : b[c] - a[c]) <= 65535u / b[3] + 1;
        }
        if (!same)
            Report(file, "16 bit samples differ from the PNG beyond what premultiplied alpha loses");
        HX_CHECK(same);
    }
}

// EXR and HDR against Blender's samples: half and float values exactly, RGBE within its precision. Every lossless
// EXR codec of one depth and channel set decodes to the same pixels.
HX_TEST(Corpus, FloatImages)
{
    const Manifest& m = Manifest::Get();
    u32 list = m.Section("float_images");
    HX_REQUIRE(m.Count(list) > 20);
    u32 matched = 0, refused = 0;
    for (u32 i = 0; i < m.Count(list); i++)
    {
        u32 e = m.Item(list, i);
        StringView file = m.Text(e, "file");
        TextureData t;
        String error;
        bool ok = Decode(file, t, error);
        if (const Unsupported* u = Skip(file))
        {
            if (ok)
                Report(file, Format("read, but listed as unsupported ({})", u->reason));
            HX_CHECK(!ok && !error.Empty());
            refused++;
            continue;
        }
        if (!ok)
        {
            Report(file, Format("not decoded: {}", error));
            HX_CHECK(ok);
            continue;
        }
        if (m.Has(e, "blender"))
            continue;
        HX_CHECK_EQ(t.width, u32(m.Number(e, "width")));
        HX_CHECK_EQ(t.height, u32(m.Number(e, "height")));
        Array<float> px;
        HX_REQUIRE(Rgba32f(t, px));
        bool rgbe = Extension(file) == StringView("hdr");
        u32 samples = m.Member(e, "samples");
        double worst = 0, worstRgbe = 0;
        const auto& tree = m.Tree();
        for (u32 s = 0; s < m.Count(samples); s++)
        {
            u32 row = m.Item(samples, s);
            double v[6];
            for (u32 k = 0; k < 6; k++)
                tree.AsDouble(tree.Item(row, k), v[k]);
            const float* p = px.Data() + (usize(v[1]) * t.width + usize(v[0])) * 4;
            if (rgbe)
            {
                // RGBE: Blender takes the mantissa as it is, the decoder the middle of its step as Radiance's own
                // reader does (encoders truncate). They differ by up to half a step, and a step is at most 1 / 128
                // of the brightest channel.
                double brightest = v[2] > v[3] ? (v[2] > v[4] ? v[2] : v[4]) : (v[3] > v[4] ? v[3] : v[4]);
                for (u32 c = 0; c < 3; c++)
                {
                    double err = (p[c] - v[2 + c]) / (brightest / 256 + 1e-30);
                    worstRgbe = std::fabs(err) > worstRgbe ? std::fabs(err) : worstRgbe;
                    worst = err < -1e-6 ? 1 : worst;
                }
                continue;
            }
            for (u32 c = 0; c < 4; c++)
                worst = p[c] != float(v[2 + c]) ? 1 : worst;
        }
        // Half and float values exactly; RGBE never below Blender's value and within half a step above it.
        bool within = worst == 0 && worstRgbe <= 1.0 + 1e-6;
        if (!within)
            Report(file, Format("differs from Blender (RGBE: up to {} half steps)", worstRgbe));
        HX_CHECK(within);
        matched += within;
    }

    // The lossless EXR codecs agree with NONE.
    const char* depths[] = {"half", "float"};
    const char* modes[] = {"rgb", "rgba"};
    const char* codecs[] = {"rle", "zips", "zip", "piz"};
    for (const char* depth : depths)
        for (const char* mode : modes)
        {
            TextureData none;
            String error;
            HX_REQUIRE(Decode(Format("Generated/Images/Blender/exr_none_{}_{}.exr", depth, mode), none, error));
            for (const char* codec : codecs)
            {
                TextureData other;
                String file = Format("Generated/Images/Blender/exr_{}_{}_{}.exr", codec, depth, mode);
                HX_REQUIRE(Decode(file, other, error));
                bool same = other.format == none.format && other.data.Size() == none.data.Size() &&
                            std::memcmp(other.data.Data(), none.data.Data(), none.data.Size()) == 0;
                if (!same)
                    Report(file, "differs from the uncompressed file");
                HX_CHECK(same);
            }
        }
    std::printf("    %u float images match Blender, %u refused as listed\n", matched, refused);
}
