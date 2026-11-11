#pragma once

#include <Helix/Core/Strings/Format.h>
#include <Helix/Core/Strings/String.h>
#include <Helix/Platform/IO/FileSystem.h>
#include <Helix/Serialization/Json/Parser.h>
#include <Helix/Serialization/Json/Tree.h>

#include <cstdio>
#include <cstdlib>

// The corpus on disk and Expected/Manifest.json, the record of what other tools read from each file (Tools/Expect.py).
namespace helix::corpus
{
    namespace json = serialization::json;

    inline String Path(StringView relative) { return Format("{}/{}", HELIX_CORPUS_DIR, relative); }

    inline bool Read(StringView relative, Array<u8>& out) { return platform::ReadFile(Path(relative), out); }

    // The extension of a path, lower case, without the dot.
    inline String Extension(StringView path)
    {
        String out;
        usize dot = path.Size();
        while (dot > 0 && path[dot - 1] != '.' && path[dot - 1] != '/')
            dot--;
        if (dot == 0 || path[dot - 1] != '.')
            return out;
        for (usize i = dot; i < path.Size(); i++)
            out.Append(char(path[i] >= 'A' && path[i] <= 'Z' ? path[i] + 32 : path[i]));
        return out;
    }

    // The folder of a path, without the last slash.
    inline StringView Folder(StringView path)
    {
        usize slash = path.Size();
        while (slash > 0 && path[slash - 1] != '/')
            slash--;
        return path.Substr(0, slash ? slash - 1 : 0);
    }

    class Manifest
    {
    public:
        static const Manifest& Get()
        {
            static Manifest m;
            return m;
        }

        // The entries of a section ("images", "float_images", "audio", "fonts", "json", "models").
        u32 Section(StringView name) const { return tree_.Find(tree_.root, name); }
        u32 Count(u32 array) const { return array == json::kNoNode ? 0 : tree_.Count(array); }
        u32 Item(u32 array, u32 i) const { return tree_.Item(array, i); }
        u32 Member(u32 object, StringView key) const { return tree_.Find(object, key); }
        bool Has(u32 object, StringView key) const { return Member(object, key) != json::kNoNode; }
        StringView Text(u32 object, StringView key) const
        {
            u32 n = Member(object, key);
            return tree_.IsString(n) ? tree_.Text(n) : StringView();
        }
        double Number(u32 object, StringView key, double fallback = 0) const
        {
            double d = fallback;
            u32 n = Member(object, key);
            if (n != json::kNoNode)
                tree_.AsDouble(n, d);
            return d;
        }
        bool Flag(u32 object, StringView key) const
        {
            bool b = false;
            u32 n = Member(object, key);
            if (n != json::kNoNode)
                tree_.AsBool(n, b);
            return b;
        }
        const json::Tree& Tree() const { return tree_; }

    private:
        Manifest()
        {
            String error;
            if (!Read("Expected/Manifest.json", bytes_) ||
                !json::Parse(StringView(reinterpret_cast<const char*>(bytes_.Data()), bytes_.Size()), tree_, error))
            {
                std::fprintf(stderr, "cannot read %s/Expected/Manifest.json: %s\n", HELIX_CORPUS_DIR, error.CStr());
                std::abort();
            }
        }

        Array<u8> bytes_;
        json::Tree tree_;
    };

    // Files of the corpus a library does not read on purpose, with the reason; each must be refused with an error, and
    // a file on this list that is read means the list is out of date.
    struct Unsupported
    {
        const char* part;
        const char* reason;
    };

    inline const Unsupported* FindUnsupported(StringView file, const Unsupported* list, usize count)
    {
        for (usize i = 0; i < count; i++)
            if (file.Find(StringView(list[i].part)) != StringView::npos)
                return &list[i];
        return nullptr;
    }
}
