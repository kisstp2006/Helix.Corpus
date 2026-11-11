#include <Support/Corpus.h>

#include <Helix/Audio/Decoding/AudioDecoder.h>
#include <Helix/Font/Font/Font.h>
#include <Helix/Font/Layout/Layout.h>
#include <Helix/Font/Outline/Outline.h>
#include <Helix/Font/Raster/Raster.h>
#include <Helix/Image/Codecs/ImageDecoder.h>
#include <Helix/Image/Texture/BlockCodec.h>
#include <Helix/Image/Texture/Convert.h>
#include <Helix/Model/Fbx/Fbx.h>
#include <Helix/Model/Gltf/Gltf.h>
#include <Helix/Model/Obj/Obj.h>
#include <Helix/Model/Scene/Convert.h>
#include <Helix/Testing/Fuzz.h>

#include <cstdlib>

using namespace helix;
using namespace helix::corpus;
using helix::testing::FuzzRound;

// Every file of the corpus is a seed of its parser: the mutations start from what real tools write.
namespace
{
    struct Seed
    {
        String path;
        Array<u8> bytes = Array<u8>();
    };

    struct Seeds
    {
        Array<Seed> images, sounds, fonts, models, documents;
    };

    void Collect(const String& folder, Seeds& out)
    {
        struct Context
        {
            const String* folder = nullptr;
            Array<String> files = Array<String>(), folders = Array<String>();
        } c;
        c.folder = &folder;
        platform::ListDirectory(Path(folder), [](const platform::DirectoryEntry& e, void* user) {
            Context& c = *static_cast<Context*>(user);
            String path = Format("{}/{}", *c.folder, e.name);
            // Seeds stay small enough for many rounds a second.
            if (e.isDirectory)
                c.folders.Push(Move(path));
            else if (e.size <= (i64(1) << 21))
                c.files.Push(Move(path));
        }, &c);
        for (const String& f : c.folders)
            Collect(f, out);
        for (String& f : c.files)
        {
            String ext = Extension(f);
            Array<Seed>* list = nullptr;
            for (const char* e : {"png", "jpg", "jpeg", "bmp", "tga", "gif", "tif", "tiff", "psd", "hdr", "exr", "dds", "ktx", "ktx2", "pvr"})
                list = ext == StringView(e) ? &out.images : list;
            for (const char* e : {"wav", "w64", "flac", "mp3", "ogg"})
                list = ext == StringView(e) ? &out.sounds : list;
            for (const char* e : {"otf", "ttf", "ttc"})
                list = ext == StringView(e) ? &out.fonts : list;
            for (const char* e : {"gltf", "glb", "fbx", "obj"})
                list = ext == StringView(e) ? &out.models : list;
            if (ext == StringView("gltf"))
            {
                Seed d;
                d.path = f;
                out.documents.Push(Move(d));
            }
            if (!list)
                continue;
            Seed s;
            s.path = Move(f);
            if (Read(s.path, s.bytes))
                list->Push(Move(s));
        }
        for (Seed& d : out.documents)
            if (d.bytes.Empty())
                Read(d.path, d.bytes);
    }

    const Seeds& All()
    {
        static const Seeds seeds = [] {
            Seeds s;
            Collect("Assets", s);
            Collect("Generated", s);
            if (s.images.Empty() || s.sounds.Empty() || s.fonts.Empty() || s.models.Empty())
                std::abort();
            return s;
        }();
        return seeds;
    }

    const Seed& Pick(FuzzRound& round, const Array<Seed>& list) { return list[round.Below(u32(list.Size()))]; }

    Array<u8> Mutated(FuzzRound& round, const Seed& seed)
    {
        Array<u8> bytes;
        bytes.Append(seed.bytes.Data(), seed.bytes.Size());
        bytes.Resize(seed.bytes.Size() + 64);
        bytes.Resize(round.Mutate(bytes.Data(), seed.bytes.Size(), bytes.Size()));
        return bytes;
    }

    struct Loader
    {
        String folder;
    };

    bool LoadNextTo(void* context, StringView uri, Array<u8>& out) { return Read(Format("{}/{}", static_cast<Loader*>(context)->folder, uri), out); }
}

HX_FUZZ(CorpusImages)
{
    const Seed& seed = Pick(round, All().images);
    round.Step("decode", seed.path.CStr());
    Array<u8> bytes = Mutated(round, seed);
    image::TextureData t;
    String error;
    if (!image::DecodeImage(Span<const u8>(bytes), t, error, Extension(seed.path)))
        return 0;
    round.Step("convert");
    image::TextureData decoded;
    const image::TextureData* src = &t;
    if (image::IsBlockFormat(t.format))
    {
        if (!image::DecompressTexture(t, decoded, error))
            return 1;
        src = &decoded;
    }
    usize n = usize(src->width) * src->height;
    if (n <= (usize(1) << 22) && image::CanConvert(src->format))
    {
        Array<float> rgba;
        rgba.Resize(n * 4);
        image::ConvertPixels(src->format, src->Pixels(), image::PixelFormat::RGBA32Float, reinterpret_cast<u8*>(rgba.Data()), n);
    }
    return n + 1;
}

HX_FUZZ(CorpusSounds)
{
    const Seed& seed = Pick(round, All().sounds);
    round.Step("open", seed.path.CStr());
    Array<u8> bytes = Mutated(round, seed);
    String error;
    auto stream = audio::OpenAudio(Span<const u8>(bytes), error, Extension(seed.path));
    if (!stream)
        return 0;
    round.Step("read");
    Array<float> block;
    block.Resize(4096 * audio::kMaxChannels);
    u64 frames = 0;
    // A damaged file may claim any length: at most a few seconds a round.
    for (u32 i = 0; i < 64; i++)
    {
        u64 got = stream->Read(block.Data(), 4096);
        frames += got;
        if (got == 0)
            break;
        if (round.Below(16) == 0)
            stream->Seek(round.Next() % (frames + 1));
    }
    return frames + 1;
}

HX_FUZZ(CorpusFonts)
{
    const Seed& seed = Pick(round, All().fonts);
    round.Step("open", seed.path.CStr());
    Array<u8> bytes = Mutated(round, seed);
    font::Font f;
    String error;
    if (!f.Open(Span<const u8>(bytes), 0, error))
        return 0;
    font::Outlines outlines;
    u64 items = 1;
    if (outlines.Open(f, error) && f.GlyphCount())
    {
        round.Step("glyphs");
        for (u32 i = 0; i < 16; i++)
        {
            u32 glyph = round.Below(f.GlyphCount());
            font::Outline o;
            if (!outlines.Get(glyph, o, error))
                continue;
            font::RasterOptions r;
            r.scale = 16.0f / float(f.Head().unitsPerEm ? f.Head().unitsPerEm : 1000);
            font::GlyphBitmap b;
            font::Rasterize(o, r, b, error);
            items += b.pixels.Size() + 1;
        }
    }
    round.Step("layout");
    for (u32 tag : {font::Tag("GSUB"), font::Tag("GPOS")})
    {
        font::Layout layout;
        layout.Read(f, tag, error);
    }
    return items;
}

HX_FUZZ(CorpusModels)
{
    const Seed& seed = Pick(round, All().models);
    round.Step("read", seed.path.CStr());
    Array<u8> bytes = Mutated(round, seed);
    model::Scene s;
    String error;
    Loader loader{String(Folder(seed.path))};
    model::ReadOptions options;
    options.context = &loader;
    options.loader = &LoadNextTo;
    String ext = Extension(seed.path);
    bool ok = ext == StringView("fbx")   ? model::ReadFbx(Span<const u8>(bytes), s, error, options)
              : ext == StringView("obj") ? model::ReadObj(Span<const u8>(bytes), s, error, options)
                                         : model::ReadGltf(Span<const u8>(bytes), s, error, options);
    if (!ok)
        return 0;
    round.Step("convert");
    model::ConvertScene(s);
    for (const model::Primitive& p : s.primitives)
        for (u32 i = 0; i < p.streams.count; i++)
        {
            const model::VertexStream& st = s.streams[p.streams.begin + i];
            if (st.offset + u64(st.components) * p.vertexCount > s.floats.Size())
                std::abort();
        }
    return s.TriangleCount() + 1;
}

HX_FUZZ(CorpusJson)
{
    const Seed& seed = Pick(round, All().documents);
    Array<u8> bytes = Mutated(round, seed);
    json::Tree tree;
    String error;
    round.Step("parse", seed.path.CStr());
    return json::Parse(StringView(reinterpret_cast<const char*>(bytes.Data()), bytes.Size()), tree, error) ? tree.nodes.Size() + 1 : 0;
}
