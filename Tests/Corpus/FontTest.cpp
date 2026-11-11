#include <Support/Corpus.h>

#include <Helix/Font/Atlas/FontAtlas.h>
#include <Helix/Font/Font/Font.h>
#include <Helix/Font/Layout/Layout.h>
#include <Helix/Font/Outline/Outline.h>
#include <Helix/Font/Raster/Raster.h>
#include <Helix/Testing/Test.h>

#include <cmath>
#include <cstdio>

using namespace helix;
using namespace helix::corpus;
using namespace helix::font;

// Every font against FreeType (through Pillow): names, units per em, the glyphs of the ASCII and a few Latin
// characters mapped, their advances exactly, and their 32 pixel coverage close (FreeType hints CFF stems, the
// rasterizer does not). Then every glyph of every font: outline, coverage and packing, the layout tables read.
HX_TEST(Corpus, Fonts)
{
    const Manifest& m = Manifest::Get();
    u32 list = m.Section("fonts");
    HX_REQUIRE(m.Count(list) > 100);
    u64 glyphs = 0, compared = 0;
    double worstInk = 0;
    for (u32 i = 0; i < m.Count(list); i++)
    {
        u32 e = m.Item(list, i);
        StringView file = m.Text(e, "file");
        Array<u8> bytes;
        HX_REQUIRE(Read(file, bytes));
        Font font;
        String error;
        if (!font.Open(Span<const u8>(bytes), 0, error))
        {
            std::printf("    %s: %s\n", String(file).CStr(), error.CStr());
            HX_CHECK(false);
            continue;
        }
        StringView family = m.Text(e, "family"), style = m.Text(e, "style");
        bool names = (font.Name(16) == family || font.Name(1) == family) && (font.Name(17) == style || font.Name(2) == style);
        if (!names)
            std::printf("    %s: names '%s' / '%s', FreeType '%s' / '%s'\n", String(file).CStr(), String(font.Name(1)).CStr(), String(font.Name(2)).CStr(),
                        String(family).CStr(), String(style).CStr());
        HX_CHECK(names);
        HX_CHECK_EQ(u32(font.Head().unitsPerEm), u32(m.Number(e, "upem")));

        Outlines outlines;
        HX_REQUIRE(outlines.Open(font, error));
        float scale = 32.0f / float(font.Head().unitsPerEm);
        u32 entries = m.Member(e, "glyphs");
        const auto& tree = m.Tree();
        for (u32 g = 0; g < m.Count(entries); g++)
        {
            u32 entry = m.Item(entries, g);
            u32 code = u32(m.Number(entry, "code"));
            u32 glyph = font.GlyphIndex(code);
            if (glyph == 0)
            {
                std::printf("    %s: U+%04X not mapped\n", String(file).CStr(), code);
                HX_CHECK(false);
                continue;
            }
            bool advance = font.HMetrics(glyph).advance == u32(m.Number(entry, "advance"));
            if (!advance)
                std::printf("    %s: U+%04X advance %u, FreeType %u\n", String(file).CStr(), code, u32(font.HMetrics(glyph).advance), u32(m.Number(entry, "advance")));
            HX_CHECK(advance);
            Outline outline;
            HX_REQUIRE(outlines.Get(glyph, outline, error));
            RasterOptions r;
            r.scale = scale;
            GlyphBitmap bitmap;
            HX_REQUIRE(Rasterize(outline, r, bitmap, error));
            double ink = 0;
            for (u8 v : bitmap.pixels)
                ink += v;
            double theirs = m.Number(entry, "ink");
            // Hinting moves stems by up to half a pixel at this size: the ink of each glyph within 12 % (plus the
            // ink of a one pixel ring for the smallest marks).
            double bound = 0.12 * theirs + 255.0 * 4;
            double diff = std::fabs(ink - theirs);
            worstInk = std::fmax(worstInk, theirs > 0 ? diff / theirs : 0);
            bool close = diff <= bound;
            if (!close)
                std::printf("    %s: U+%04X ink %.0f, FreeType %.0f\n", String(file).CStr(), code, ink, theirs);
            HX_CHECK(close);
            compared++;
            (void)tree;
        }

        // Every glyph: an outline that is finite, a coverage bitmap, a place in the atlas.
        FontAtlas atlas(1024, 1024, 1);
        for (u32 glyph = 0; glyph < font.GlyphCount(); glyph++)
        {
            Outline outline;
            bool ok = outlines.Get(glyph, outline, error) && outline.Finite();
            if (!ok)
                std::printf("    %s: glyph %u: %s\n", String(file).CStr(), glyph, error.CStr());
            HX_CHECK(ok);
            RasterOptions r;
            r.scale = scale;
            GlyphBitmap bitmap;
            HX_CHECK(Rasterize(outline, r, bitmap, error));
            if (bitmap.width && bitmap.height)
            {
                AtlasPlace place;
                HX_CHECK(atlas.Add(FontAtlas::Key(glyph, 32, 0), bitmap, place));
            }
            glyphs++;
        }
        for (u32 tag : {Tag("GSUB"), Tag("GPOS")})
            if (font.HasTable(tag))
            {
                Layout layout;
                bool read = layout.Read(font, tag, error);
                if (!read)
                    std::printf("    %s: layout table %08x: %s\n", String(file).CStr(), tag, error.CStr());
                HX_CHECK(read);
            }
    }
    std::printf("    %llu glyphs read and packed, %llu compared with FreeType (ink within %.1f %%)\n", (unsigned long long)glyphs, (unsigned long long)compared,
                worstInk * 100);
}
