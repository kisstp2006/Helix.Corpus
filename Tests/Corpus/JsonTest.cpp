#include <Support/Corpus.h>

#include <Helix/Serialization/Json/Writer.h>
#include <Helix/Testing/Test.h>

#include <cmath>
#include <cstdio>
#include <cstring>

using namespace helix;
using namespace helix::corpus;

namespace
{
    // The statistics Expect.py takes with Python's json: counts of every kind, the sum of the numbers, the depth, and an
    // FNV-1a hash of every key (closed by a 0 byte) and string (closed by a 1 byte) in document order.
    struct Stats
    {
        u64 objects = 0, arrays = 0, strings = 0, numbers = 0, bools = 0, nulls = 0, depth = 0;
        double sum = 0;
        u64 hash = 0xCBF29CE484222325ull;

        void Hash(StringView text, u8 end)
        {
            for (char c : text)
                hash = (hash ^ u8(c)) * 0x100000001B3ull;
            hash = (hash ^ end) * 0x100000001B3ull;
        }

        void Walk(const json::Tree& t, u32 node, u64 level)
        {
            depth = level > depth ? level : depth;
            switch (t.Kind(node))
            {
            case json::NodeKind::Object:
                objects++;
                for (u32 i = 0; i < t.Count(node); i++)
                {
                    Hash(t.Text(t.Key(node, i)), 0);
                    Walk(t, t.Value(node, i), level + 1);
                }
                break;
            case json::NodeKind::Array:
                arrays++;
                for (u32 i = 0; i < t.Count(node); i++)
                    Walk(t, t.Item(node, i), level + 1);
                break;
            case json::NodeKind::String:
                strings++;
                Hash(t.Text(node), 1);
                break;
            case json::NodeKind::Number:
            {
                numbers++;
                double d = 0;
                t.AsDouble(node, d);
                sum += d;
                break;
            }
            case json::NodeKind::True:
            case json::NodeKind::False: bools++; break;
            case json::NodeKind::Null: nulls++; break;
            }
        }
    };

    StringView JsonText(StringView file, const Array<u8>& bytes)
    {
        if (Extension(file) != StringView("glb") || bytes.Size() < 20)
            return StringView(reinterpret_cast<const char*>(bytes.Data()), bytes.Size());
        u32 length;
        std::memcpy(&length, bytes.Data() + 12, 4);
        return StringView(reinterpret_cast<const char*>(bytes.Data()) + 20, length <= bytes.Size() - 20 ? length : 0);
    }
}

// Every glTF document (and GLB JSON chunk) gives the tree Python's json gives; written by our writer and read again it
// gives the same tree.
HX_TEST(Corpus, Json)
{
    const Manifest& m = Manifest::Get();
    u32 list = m.Section("json");
    HX_REQUIRE(m.Count(list) > 40);
    u32 same = 0;
    for (u32 i = 0; i < m.Count(list); i++)
    {
        u32 e = m.Item(list, i);
        StringView file = m.Text(e, "file");
        Array<u8> bytes;
        HX_REQUIRE(Read(file, bytes));
        json::Tree tree;
        String error;
        if (!json::Parse(JsonText(file, bytes), tree, error))
        {
            std::printf("    %s: %s\n", String(file).CStr(), error.CStr());
            HX_CHECK(false);
            continue;
        }
        Stats s;
        s.Walk(tree, tree.root, 1);
        char hash[17];
        std::snprintf(hash, sizeof(hash), "%016llx", (unsigned long long)s.hash);
        double sum = m.Number(e, "sum");
        bool equal = s.objects == u64(m.Number(e, "objects")) && s.arrays == u64(m.Number(e, "arrays")) && s.strings == u64(m.Number(e, "strings")) &&
                     s.numbers == u64(m.Number(e, "numbers")) && s.bools == u64(m.Number(e, "bools")) && s.nulls == u64(m.Number(e, "nulls")) &&
                     s.depth == u64(m.Number(e, "depth")) && m.Text(e, "hash") == StringView(hash) &&
                     std::fabs(s.sum - sum) <= 1e-9 * (std::fabs(sum) + 1);
        if (!equal)
            std::printf("    %s: %llu objects %llu arrays %llu strings %llu numbers, sum %.17g, hash %s\n", String(file).CStr(), (unsigned long long)s.objects,
                        (unsigned long long)s.arrays, (unsigned long long)s.strings, (unsigned long long)s.numbers, s.sum, hash);
        HX_CHECK(equal);

        // Written and read back: the same statistics.
        String text;
        json::Write(tree, text);
        json::Tree again;
        HX_REQUIRE(json::Parse(text, again, error));
        Stats t;
        t.Walk(again, again.root, 1);
        bool roundTrip = t.hash == s.hash && t.numbers == s.numbers && t.sum == s.sum && t.objects == s.objects;
        HX_CHECK(roundTrip);
        same += equal && roundTrip;
    }
    std::printf("    %u documents equal to Python's and round trip\n", same);
}
