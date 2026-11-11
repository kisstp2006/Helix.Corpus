#include <Support/Corpus.h>

#include <Helix/Image/Codecs/ImageDecoder.h>
#include <Helix/Image/Codecs/ImageEncoder.h>
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
    bool DecodeFile(StringView file, TextureData& out)
    {
        Array<u8> bytes;
        String error;
        return Read(file, bytes) && DecodeImage(Span<const u8>(bytes), out, error, Extension(file));
    }

    bool Rgba(const TextureData& t, PixelFormat to, Array<u8>& out)
    {
        usize n = usize(t.width) * t.height;
        out.Resize(n * FormatInfo(to).bytesPerBlock);
        return ConvertPixels(t.format, t.Pixels(), to, out.Data(), n);
    }
}

// Our encoders on corpus images: PNG, TGA and every EXR codec give back the pixels exactly, JPG within its quality.
// The files are written to Encoded/ in the working directory, for Tools/CheckEncoded.py to read with Pillow and
// Blender.
HX_TEST(Corpus, Encoders)
{
    TextureData photo, hdr;
    HX_REQUIRE(DecodeFile("Generated/Images/Pillow/png_rgba8.png", photo));
    HX_REQUIRE(DecodeFile("Generated/Images/Blender/exr_none_float_rgba.exr", hdr));
    HX_CHECK(platform::CreateDirectories("Encoded"));

    struct Case
    {
        const char* name;
        const char* extension;
        const TextureData* source;
        u32 quality, exrFlags;
    };
    const Case cases[] = {
        {"photo.png", "png", &photo, 75, 0},
        {"photo.tga", "tga", &photo, 75, 0},
        {"photo_q90.jpg", "jpg", &photo, 90, 0},
        {"photo_q50.jpg", "jpg", &photo, 50, 0},
        {"hdr_none_half.exr", "exr", &hdr, 75, ExrNone},
        {"hdr_rle_half.exr", "exr", &hdr, 75, ExrCompressRle},
        {"hdr_zip_half.exr", "exr", &hdr, 75, ExrCompressZip},
        {"hdr_piz_half.exr", "exr", &hdr, 75, ExrCompressPiz},
        {"hdr_none_float.exr", "exr", &hdr, 75, ExrOutputAsFloat},
        {"hdr_zip_float.exr", "exr", &hdr, 75, ExrOutputAsFloat | ExrCompressZip},
        {"hdr_piz_float.exr", "exr", &hdr, 75, ExrOutputAsFloat | ExrCompressPiz},
    };
    for (const Case& c : cases)
    {
        EncodeOptions options;
        options.quality = c.quality;
        options.exrFlags = c.exrFlags;
        Array<u8> file;
        String error;
        HX_REQUIRE(EncodeImage(*c.source, c.extension, file, error, options));
        HX_CHECK(platform::WriteFile(Format("Encoded/{}", c.name), Span<const u8>(file)));
        TextureData back;
        HX_REQUIRE(DecodeImage(Span<const u8>(file), back, error, c.extension));
        HX_REQUIRE(back.width == c.source->width && back.height == c.source->height);
        bool ok;
        if (c.source == &hdr)
        {
            // Half files keep the source rounded to half, float files the source itself.
            Array<u8> a, b;
            PixelFormat as = (c.exrFlags & ExrOutputAsFloat) ? PixelFormat::RGBA32Float : PixelFormat::RGBA16Float;
            HX_REQUIRE(Rgba(*c.source, as, a) && Rgba(back, as, b));
            ok = std::memcmp(a.Data(), b.Data(), a.Size()) == 0;
        }
        else
        {
            Array<u8> a, b;
            HX_REQUIRE(Rgba(*c.source, PixelFormat::RGBA8, a) && Rgba(back, PixelFormat::RGBA8, b));
            double squares = 0;
            for (usize i = 0; i < a.Size(); i++)
            {
                // JPG has no alpha: it reads back opaque.
                if (i % 4 == 3 && StringView(c.extension) == StringView("jpg"))
                    continue;
                double d = double(a[i]) - b[i];
                squares += d * d;
            }
            double psnr = squares > 0 ? 10 * std::log10(255.0 * 255.0 * double(a.Size()) * 0.75 / squares) : 999;
            // A sanity bound; Tools/CheckEncoded.py compares with libjpeg at the same quality.
            ok = StringView(c.extension) == StringView("jpg") ? psnr >= (c.quality >= 90 ? 36 : 30) : squares == 0;
            if (StringView(c.extension) == StringView("jpg"))
                std::printf("    %s: PSNR %.1f dB\n", c.name, psnr);
        }
        if (!ok)
            std::printf("    %s: the pixels do not come back\n", c.name);
        HX_CHECK(ok);
    }
}
