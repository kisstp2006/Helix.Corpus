#include <Support/Corpus.h>

#include <Helix/Model/Fbx/Fbx.h>
#include <Helix/Model/Gltf/Gltf.h>
#include <Helix/Model/Mesh/Mesh.h>
#include <Helix/Model/Obj/Obj.h>
#include <Helix/Model/Scene/Convert.h>
#include <Helix/Testing/Test.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

using namespace helix;
using namespace helix::corpus;
using namespace helix::model;

namespace
{
    // Files the model readers refuse on purpose.
    constexpr Unsupported kUnsupported[] = {
        {"Avocado/glTF-Draco/", "KHR_draco_mesh_compression is required"},
        {"MeshoptCubeTest/glTF-Meshopt/", "KHR_meshopt_compression is required"},
        {"CubeVisibility/", "KHR_node_visibility is required"},
        {"LightVisibility/", "KHR_node_visibility is required"},
    };

    // Files read without an extension they use (it is not required): the warning must name it, and the bounds are not
    // compared (Blender places the instances).
    constexpr Unsupported kWithoutExtension[] = {
        {"SimpleInstancing/", "EXT_mesh_gpu_instancing"},
    };

    struct Loader
    {
        String folder;
    };

    bool LoadNextTo(void* context, StringView uri, Array<u8>& out) { return Read(Format("{}/{}", static_cast<Loader*>(context)->folder, uri), out); }

    bool ReadModel(StringView file, Scene& scene, String& error)
    {
        Array<u8> bytes;
        if (!Read(file, bytes))
        {
            error = "cannot read the file";
            return false;
        }
        Loader loader{String(Folder(file))};
        ReadOptions options;
        options.context = &loader;
        options.loader = &LoadNextTo;
        String ext = Extension(file);
        if (ext == StringView("fbx"))
            return ReadFbx(Span<const u8>(bytes), scene, error, options);
        if (ext == StringView("obj"))
            return ReadObj(Span<const u8>(bytes), scene, error, options);
        return ReadGltf(Span<const u8>(bytes), scene, error, options);
    }

    // Column major 4x4 in double.
    struct Matrix
    {
        double m[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

        Matrix operator*(const Matrix& b) const
        {
            Matrix r;
            for (u32 c = 0; c < 4; c++)
                for (u32 row = 0; row < 4; row++)
                {
                    double s = 0;
                    for (u32 k = 0; k < 4; k++)
                        s += m[k * 4 + row] * b.m[c * 4 + k];
                    r.m[c * 4 + row] = s;
                }
            return r;
        }
    };

    Matrix Local(const Node& n)
    {
        Matrix r;
        if (n.hasMatrix)
        {
            for (u32 i = 0; i < 16; i++)
                r.m[i] = n.matrix[i];
            return r;
        }
        double x = n.rotation[0], y = n.rotation[1], z = n.rotation[2], w = n.rotation[3];
        double rot[9] = {1 - 2 * (y * y + z * z), 2 * (x * y + z * w),     2 * (x * z - y * w),     2 * (x * y - z * w), 1 - 2 * (x * x + z * z),
                         2 * (y * z + x * w),     2 * (x * z + y * w),     2 * (y * z - x * w),     1 - 2 * (x * x + y * y)};
        for (u32 c = 0; c < 3; c++)
            for (u32 row = 0; row < 3; row++)
                r.m[c * 4 + row] = rot[c * 3 + row] * n.scale[c];
        for (u32 row = 0; row < 3; row++)
            r.m[12 + row] = n.translation[row];
        return r;
    }

    struct Bounds
    {
        double lo[3] = {1e300, 1e300, 1e300}, hi[3] = {-1e300, -1e300, -1e300};
        bool Empty() const { return lo[0] > hi[0]; }
        double Extent() const { return Empty() ? 0 : std::fmax(hi[0] - lo[0], std::fmax(hi[1] - lo[1], hi[2] - lo[2])); }
    };

    // The vertices of every mesh instance in world space.
    void WorldBounds(const Scene& s, u32 node, const Matrix& parent, Bounds& b)
    {
        const Node& n = s.nodes[node];
        Matrix world = parent * Local(n);
        if (n.mesh != kNone)
        {
            const Mesh& mesh = s.meshes[n.mesh];
            for (u32 p = 0; p < mesh.primitives.count; p++)
            {
                const Primitive& prim = s.primitives[mesh.primitives.begin + p];
                const VertexStream* pos = s.Find(prim, Semantic::Position);
                if (!pos)
                    continue;
                const float* v = s.Data(*pos);
                for (u32 i = 0; i < prim.vertexCount; i++)
                {
                    const float* q = v + usize(i) * pos->components;
                    for (u32 row = 0; row < 3; row++)
                    {
                        double w = world.m[row] * q[0] + world.m[4 + row] * q[1] + world.m[8 + row] * q[2] + world.m[12 + row];
                        b.lo[row] = std::fmin(b.lo[row], w), b.hi[row] = std::fmax(b.hi[row], w);
                    }
                }
            }
        }
        for (u32 c = 0; c < n.children.count; c++)
            WorldBounds(s, s.nodeChildren[n.children.begin + c], world, b);
    }

    Bounds SceneBounds(const Scene& s)
    {
        Bounds b;
        for (u32 r : s.roots)
            WorldBounds(s, r, Matrix(), b);
        return b;
    }

    // Blender's bounds (z up) in the engine frame: x = -x, y = z, z = -y.
    Bounds FromBlender(const Manifest& m, u32 entry)
    {
        Bounds b;
        u32 bounds = m.Member(entry, "bounds");
        const auto& t = m.Tree();
        double lo[3], hi[3];
        for (u32 k = 0; k < 3; k++)
        {
            t.AsDouble(t.Item(t.Item(bounds, 0), k), lo[k]);
            t.AsDouble(t.Item(t.Item(bounds, 1), k), hi[k]);
        }
        b.lo[0] = -hi[0], b.hi[0] = -lo[0];
        b.lo[1] = lo[2], b.hi[1] = hi[2];
        b.lo[2] = -hi[1], b.hi[2] = -lo[1];
        return b;
    }

    bool Close(const Bounds& a, const Bounds& b, double& worst)
    {
        double tolerance = 1e-4 * std::fmax(a.Extent(), b.Extent()) + 1e-5;
        worst = 0;
        for (u32 k = 0; k < 3; k++)
            worst = std::fmax(worst, std::fmax(std::fabs(a.lo[k] - b.lo[k]), std::fabs(a.hi[k] - b.hi[k])));
        return worst <= tolerance;
    }

    // Every stream and index range inside the pools.
    bool Valid(const Scene& s)
    {
        for (const Primitive& p : s.primitives)
        {
            for (u32 i = 0; i < p.streams.count; i++)
            {
                const VertexStream& st = s.streams[p.streams.begin + i];
                if (st.offset + u64(st.components) * p.vertexCount > s.floats.Size())
                    return false;
            }
            if (u64(p.indices.begin) + p.indices.count > s.indices.Size())
                return false;
        }
        return true;
    }

    // Triangles as position bits, each rotated to its smallest corner, sorted: what reordering, welding and splitting
    // keep.
    struct Tri
    {
        u32 v[9];
        bool operator<(const Tri& o) const { return std::memcmp(v, o.v, sizeof(v)) < 0; }
        bool operator==(const Tri& o) const { return std::memcmp(v, o.v, sizeof(v)) == 0; }
    };

    Array<Tri> Geometry(const Scene& s, u32 primitive)
    {
        Array<Tri> out;
        const Primitive& p = s.primitives[primitive];
        const VertexStream* st = s.Find(p, Semantic::Position);
        if (p.topology != Topology::Triangles || !st)
            return out;
        u32 n = p.indices.count ? p.indices.count : p.vertexCount;
        for (u32 i = 0; i + 2 < n; i += 3)
        {
            u32 c[3][3];
            bool valid = true;
            for (u32 k = 0; k < 3; k++)
            {
                u32 idx = p.indices.count ? s.indices[p.indices.begin + i + k] : i + k;
                valid = valid && idx < p.vertexCount;
                for (u32 a = 0; a < 3 && valid; a++)
                {
                    float f = s.Data(*st)[usize(idx) * st->components + a];
                    f = f == 0 ? 0.0f : f;
                    std::memcpy(&c[k][a], &f, 4);
                }
            }
            if (!valid)
                continue;
            u32 first = 0;
            for (u32 k = 1; k < 3; k++)
                if (std::memcmp(c[k], c[first], sizeof(c[k])) < 0)
                    first = k;
            Tri t;
            for (u32 k = 0; k < 3; k++)
                std::memcpy(t.v + k * 3, c[(first + k) % 3], sizeof(c[0]));
            out.Push(t);
        }
        std::sort(out.begin(), out.end());
        return out;
    }

    bool Same(const Array<Tri>& a, const Array<Tri>& b)
    {
        if (a.Size() != b.Size())
            return false;
        for (usize i = 0; i < a.Size(); i++)
            if (!(a[i] == b[i]))
                return false;
        return true;
    }

    // The mesh processing of the importer on every triangle primitive, with the invariants of each step.
    bool Process(Scene& s, StringView file)
    {
        bool ok = true;
        auto fail = [&](u32 p, const char* what) {
            std::printf("    %s: primitive %u: %s\n", String(file).CStr(), p, what);
            ok = false;
        };
        u32 count = u32(s.primitives.Size());
        for (u32 p = 0; p < count; p++)
        {
            ToTriangleList(s, p);
            if (s.primitives[p].topology != Topology::Triangles || !s.Find(s.primitives[p], Semantic::Position))
                continue;
            Array<Tri> before = Geometry(s, p);
            WeldVertices(s, p);
            CalculateNormals(s, p, 60);
            if (s.Find(s.primitives[p], Semantic::TexCoord))
                CalculateTangents(s, p);
            OptimizeVertexCache(s, p);
            OptimizeOverdraw(s, p);
            OptimizeVertexFetch(s, p);
            if (!Same(Geometry(s, p), before))
                fail(p, "the geometry changed in welding, normals, tangents or reordering");
            const Primitive& prim = s.primitives[p];
            Array<float> positions;
            const VertexStream* pos = s.Find(prim, Semantic::Position);
            for (u32 v = 0; v < prim.vertexCount; v++)
                positions.Append(s.Data(*pos) + usize(v) * pos->components, 3);
            Array<u32> indices;
            indices.Append(s.indices.Data() + prim.indices.begin, prim.indices.count);
            Meshlets meshlets;
            BuildMeshlets(positions.Data(), prim.vertexCount, indices.Data(), indices.Size(), meshlets);
            u64 triangles = 0;
            for (const Meshlet& ml : meshlets.meshlets)
            {
                triangles += ml.triangleCount;
                if (ml.vertexCount > kMeshletVertices || ml.triangleCount > kMeshletTriangles)
                    fail(p, "a meshlet over its limits");
            }
            if (triangles != indices.Size() / 3)
                fail(p, "meshlets lost triangles");
            usize was = s.primitives[p].indices.count;
            SimplifyResult r = Simplify(s, p, 0.5f, 0.02f);
            if (r.indexCount > was || r.indexCount % 3)
                fail(p, "simplification grew the mesh");
            for (u32 t = 0; t < s.primitives[p].targets.count; t++)
            {
                const MorphTarget& mt = s.morphTargets[s.primitives[p].targets.begin + t];
                for (u32 k = 0; k < mt.streams.count; k++)
                {
                    const VertexStream& st = s.streams[mt.streams.begin + k];
                    CompressedDeltas packed;
                    CompressDeltas(s.Data(st), s.primitives[p].vertexCount, st.components, 1e-5f, packed);
                    Array<float> back;
                    DecompressDeltas(packed, s.primitives[p].vertexCount, back);
                    for (usize i = 0; i < back.Size(); i++)
                    {
                        float d = s.Data(st)[i];
                        if (std::isfinite(d) && std::fabs(back[i] - d) > std::fmax(packed.scale * 0.51f, 1e-5f))
                        {
                            fail(p, "blend shape deltas beyond their bound");
                            break;
                        }
                    }
                }
            }
        }
        Compact(s);
        if (!Valid(s))
            fail(0, "the scene is broken after Compact");
        return ok;
    }

    u32 Shapes(const Scene& s)
    {
        u32 n = 0;
        for (const Mesh& m : s.meshes)
            n += m.primitives.count ? s.primitives[m.primitives.begin].targets.count : 0;
        return n;
    }
}

// Every model against Blender: triangles, cameras, lights and (glTF) meshes and shape keys; the world bounds of static
// files after the conversion to the engine frame; then the importer's mesh processing on every primitive.
HX_TEST(Corpus, Models)
{
    const Manifest& m = Manifest::Get();
    u32 list = m.Section("models");
    HX_REQUIRE(m.Count(list) > 50);
    u32 matched = 0, refused = 0, bounded = 0;
    double worstBounds = 0;
    for (u32 i = 0; i < m.Count(list); i++)
    {
        u32 e = m.Item(list, i);
        StringView file = m.Text(e, "file");
        Scene s;
        String error;
        bool ok = ReadModel(file, s, error);
        if (const Unsupported* u = FindUnsupported(file, kUnsupported, sizeof(kUnsupported) / sizeof(kUnsupported[0])))
        {
            if (ok)
                std::printf("    %s: read, but listed as unsupported (%s)\n", String(file).CStr(), u->reason);
            HX_CHECK(!ok && !error.Empty());
            refused++;
            continue;
        }
        if (!ok)
        {
            std::printf("    %s: not read: %s\n", String(file).CStr(), error.CStr());
            HX_CHECK(ok);
            continue;
        }
        if (m.Has(e, "blender"))
            continue;
        const Unsupported* without = FindUnsupported(file, kWithoutExtension, sizeof(kWithoutExtension) / sizeof(kWithoutExtension[0]));
        if (without)
        {
            bool warned = false;
            for (const String& w : s.warnings)
                warned = warned || StringView(w).Contains(StringView(without->reason));
            HX_CHECK(warned);
        }
        bool gltf = Extension(file) == StringView("gltf") || Extension(file) == StringView("glb");
        u64 triangles = s.TriangleCount();
        bool counts = triangles == u64(m.Number(e, "triangles")) && s.cameras.Size() == usize(m.Number(e, "cameras")) &&
                      s.lights.Size() == usize(m.Number(e, "lights"));
        if (gltf)
            counts = counts && s.meshes.Size() == usize(m.Number(e, "meshes")) && Shapes(s) == u32(m.Number(e, "shapes"));
        if (!counts)
            std::printf("    %s: %llu triangles %zu meshes %zu cameras %zu lights %u shapes; Blender %.0f %.0f %.0f %.0f %.0f\n", String(file).CStr(),
                        (unsigned long long)triangles, s.meshes.Size(), s.cameras.Size(), s.lights.Size(), Shapes(s), m.Number(e, "triangles"),
                        m.Number(e, "meshes"), m.Number(e, "cameras"), m.Number(e, "lights"), m.Number(e, "shapes"));
        HX_CHECK(counts);
        ConvertScene(s);
        if (m.Has(e, "bounds") && !without)
        {
            Bounds ours = SceneBounds(s), theirs = FromBlender(m, e);
            double worst = 0;
            bool close = Close(ours, theirs, worst);
            worstBounds = std::fmax(worstBounds, worst / std::fmax(theirs.Extent(), 1e-9));
            if (!close)
                std::printf("    %s: bounds (%g %g %g)-(%g %g %g), Blender (%g %g %g)-(%g %g %g)\n", String(file).CStr(), ours.lo[0], ours.lo[1], ours.lo[2], ours.hi[0],
                            ours.hi[1], ours.hi[2], theirs.lo[0], theirs.lo[1], theirs.lo[2], theirs.hi[0], theirs.hi[1], theirs.hi[2]);
            HX_CHECK(close);
            bounded += close;
        }
        HX_CHECK(Process(s, file));
        matched += counts;
    }
    std::printf("    %u models match Blender's counts, %u bounds (within %.2g of the size), %u refused as listed\n", matched, bounded, worstBounds, refused);
}

// The tangents files carry (from MikkTSpace in their tools) against ours, corner by corner: the same direction nearly
// everywhere, and one sign convention for the whole file (Blender's own MikkTSpace: the same sign).
HX_TEST(Corpus, TangentsAgainstTheFiles)
{
    const char* files[] = {"Assets/Models/Khronos/Suzanne/glTF/Suzanne.gltf",
                           "Assets/Models/Khronos/AnimatedCube/glTF/AnimatedCube.gltf",
                           "Assets/Models/Khronos/TwoSidedPlane/glTF/TwoSidedPlane.gltf",
                           "Assets/Models/Khronos/AnisotropyStrengthTest/glTF-Binary/AnisotropyStrengthTest.glb",
                           "Assets/Models/Khronos/AnisotropyDiscTest/glTF-Binary/AnisotropyDiscTest.glb",
                           "Generated/Models/Blender/Suzanne/Suzanne_tangents.glb",
                           "Generated/Models/Blender/NormalTangentTest/NormalTangentTest_tangents.glb",
                           "Generated/Models/Blender/CrateFromGltf/CrateFromGltf_tangents.glb"};
    u32 compared = 0;
    for (const char* file : files)
    {
        Scene s;
        String error;
        HX_REQUIRE(ReadModel(file, s, error));
        // In the engine's frame and texture coordinates, where the importer computes tangents.
        ConvertScene(s);
        u64 corners = 0, agree = 0, signs = 0, fileVertices = 0, skewed = 0, histogram[4] = {};
        // Blender's own MikkTSpace: every corner within 0.8 degrees and the same sign; other tools within 8 degrees.
        bool blender = StringView(file).Contains(StringView("Generated/Models/Blender/"));
        for (u32 p = 0; p < s.primitives.Size(); p++)
        {
            ToTriangleList(s, p);
            const Primitive& prim = s.primitives[p];
            const VertexStream* t = s.Find(prim, Semantic::Tangent);
            const VertexStream* nrm = s.Find(prim, Semantic::Normal);
            if (!t || !nrm || !s.Find(prim, Semantic::TexCoord) || prim.topology != Topology::Triangles)
                continue;
            for (u32 v = 0; v < prim.vertexCount; v++)
            {
                const float* n = s.Data(*nrm) + usize(v) * nrm->components;
                const float* x = s.Data(*t) + usize(v) * t->components;
                fileVertices++;
                skewed += std::fabs(n[0] * x[0] + n[1] * x[1] + n[2] * x[2]) > 0.01f;
            }
            Scene ours = s;
            HX_REQUIRE(CalculateTangents(ours, p));
            const Primitive& q = ours.primitives[p];
            const VertexStream* mine = ours.Find(q, Semantic::Tangent);
            // Corner i of the new index range is corner i of the old one (no triangle had an index out of range).
            u32 n = prim.indices.count ? prim.indices.count : prim.vertexCount;
            HX_REQUIRE(q.indices.count == n - n % 3);
            for (u32 c = 0; c < q.indices.count; c++)
            {
                u32 before = prim.indices.count ? s.indices[prim.indices.begin + c] : c;
                const float* a = s.Data(*t) + usize(before) * t->components;
                const float* b = ours.Data(*mine) + usize(ours.indices[q.indices.begin + c]) * 4;
                double d = a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
                corners++;
                agree += d >= (blender ? 0.9999 : 0.99);
                histogram[d >= 0.99 ? 0 : d >= 0.94 ? 1 : d >= 0.7 ? 2 : 3]++;
                signs += a[3] == b[3];
            }
        }
        // MikkTSpace tangents lie in the normal's plane; tangents that do not were made another way and tell nothing.
        if (skewed * 100 > fileVertices)
        {
            std::printf("    %s: %llu of %llu file tangents leave the normal's plane (not MikkTSpace), not compared\n", file, (unsigned long long)skewed,
                        (unsigned long long)fileVertices);
            continue;
        }
        compared++;
        double share = corners ? double(agree) / double(corners) : 0, signShare = corners ? double(signs) / double(corners) : 0;
        std::printf("    %s: %llu corners, %.1f %% within the bound (%llu to 20, %llu to 45, %llu beyond), %.1f %% same sign\n", file,
                    (unsigned long long)corners, share * 100, (unsigned long long)histogram[1], (unsigned long long)histogram[2],
                    (unsigned long long)histogram[3], signShare * 100);
        HX_CHECK(corners > 0 && (blender ? share == 1 && signShare == 1 : share >= 0.95 && (signShare >= 0.99 || signShare <= 0.01)));
    }
    HX_CHECK(compared >= 5u);
}
