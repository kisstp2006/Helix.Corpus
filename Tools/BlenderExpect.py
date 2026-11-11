"""Run by Expect.py inside Blender: what Blender reads from the float images (EXR, HDR) and the models of the corpus.

    blender --background --factory-startup --python Tools/BlenderExpect.py -- <corpus root> <output json>

Float images: size, channels, the mean and a 16 x 8 grid of samples (rows counted from the top). Models: triangles
over the meshes (each mesh once, however many objects use it), meshes, bones, cameras, lights, shape keys, whether
there is animation, and for files without animation or skins the world bounds in Blender's frame (z up, meters).
"""

import json
import os
import sys

import bpy

ROOT, OUTPUT = sys.argv[sys.argv.index("--") + 1:][:2]


def rel(p):
    return os.path.relpath(p, ROOT).replace("\\", "/")


def files(exts):
    found = []
    for top in ("Assets", "Generated"):
        for dirpath, _, names in os.walk(os.path.join(ROOT, top)):
            for n in names:
                if os.path.splitext(n)[1].lower() in exts:
                    found.append(os.path.join(dirpath, n))
    return sorted(found, key=rel)


def float_images():
    entries = []
    for p in files({".exr", ".hdr"}):
        e = {"file": rel(p)}
        try:
            img = bpy.data.images.load(p, check_existing=False)
            img.colorspace_settings.name = "Non-Color"
            w, h = img.size
            if w == 0 or h == 0:
                raise RuntimeError("no pixels")
            px = [0.0] * (w * h * 4)
            img.pixels.foreach_get(px)
            samples = []
            for gy in range(8):
                for gx in range(16):
                    x, y = (gx * 2 + 1) * w // 32, (gy * 2 + 1) * h // 16
                    i = ((h - 1 - y) * w + x) * 4
                    samples.append([x, y] + px[i:i + 4])
            mean = [sum(px[c::4]) / (w * h) for c in range(4)]
            e.update(width=w, height=h, channels=img.channels, mean=mean, samples=samples)
            bpy.data.images.remove(img)
        except Exception as ex:
            e["blender"] = "cannot read: %s" % ex
        entries.append(e)
    return entries


def models():
    entries = []
    for p in files({".gltf", ".glb", ".fbx", ".obj"}):
        bpy.ops.wm.read_factory_settings(use_empty=True)
        e = {"file": rel(p)}
        ext = os.path.splitext(p)[1].lower()
        try:
            if ext in (".gltf", ".glb"):
                bpy.ops.import_scene.gltf(filepath=p)
            elif ext == ".fbx":
                bpy.ops.import_scene.fbx(filepath=p)
            else:
                bpy.ops.wm.obj_import(filepath=p)
        except Exception as ex:
            e["blender"] = "cannot import: %s" % str(ex).splitlines()[0]
            entries.append(e)
            continue
        # Meshes the importer made only to draw bones (custom shapes) are not the file's.
        shapes_of_bones = {b.custom_shape for o in bpy.data.objects if o.type == "ARMATURE" for b in o.pose.bones if b.custom_shape}
        meshes = {o.data for o in bpy.data.objects if o.type == "MESH" and o not in shapes_of_bones}
        triangles = 0
        shapes = 0
        for m in meshes:
            m.calc_loop_triangles()
            triangles += len(m.loop_triangles)
            if m.shape_keys:
                shapes += len(m.shape_keys.key_blocks) - 1
        animated = len(bpy.data.actions) > 0
        skinned = any(o.type == "ARMATURE" for o in bpy.data.objects)
        e.update(triangles=triangles, meshes=len(meshes), bones=sum(len(a.bones) for a in bpy.data.armatures),
                 cameras=len(bpy.data.cameras), lights=len(bpy.data.lights), shapes=shapes, animated=animated, skinned=skinned)
        if not animated and not skinned:
            lo, hi = [1e30] * 3, [-1e30] * 3
            for o in bpy.data.objects:
                if o.type != "MESH":
                    continue
                mw = o.matrix_world
                for v in o.data.vertices:
                    w = mw @ v.co
                    for k in range(3):
                        lo[k], hi[k] = min(lo[k], w[k]), max(hi[k], w[k])
            if lo[0] <= hi[0]:
                e["bounds"] = [lo, hi]
        entries.append(e)
    return entries


result = {"float_images": float_images(), "models": models()}
with open(OUTPUT, "w") as f:
    json.dump(result, f)
