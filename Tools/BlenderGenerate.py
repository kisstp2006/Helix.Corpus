"""Run by Generate.py inside Blender: images Blender writes (EXR in every codec, HDR, TIFF, PNG, TGA, BMP, JPEG) from
the Poly Haven HDRI, and models through Blender's importers and exporters (FBX, OBJ, GLB, glTF).

    blender --background --factory-startup --python Tools/BlenderGenerate.py -- <corpus root>
"""

import os
import sys

import bpy

ROOT = sys.argv[sys.argv.index("--") + 1]
ASSETS = os.path.join(ROOT, "Assets")
OUT = os.path.join(ROOT, "Generated")


def out(*parts):
    p = os.path.join(OUT, *parts)
    os.makedirs(os.path.dirname(p), exist_ok=True)
    return p


def reset():
    bpy.ops.wm.read_factory_settings(use_empty=True)


def settings(scene, fmt, **kw):
    s = scene.render.image_settings
    if hasattr(s, "media_type"):
        s.media_type = "MULTI_LAYER_IMAGE" if fmt == "OPEN_EXR_MULTILAYER" else "IMAGE"
    s.file_format = fmt
    for k, v in kw.items():
        setattr(s, k, v)


def images():
    reset()
    scene = bpy.context.scene
    scene.view_settings.view_transform = "Standard"
    scene.view_settings.look = "None"
    hdri = bpy.data.images.load(os.path.join(ASSETS, "Images", "PolyHaven", "brown_photostudio_02_1k.exr"))
    hdri.scale(128, 64)
    # An alpha ramp, so RGBA files carry something.
    w, h = hdri.size
    px = list(hdri.pixels)
    for y in range(h):
        for x in range(w):
            px[(y * w + x) * 4 + 3] = x / (w - 1)
    img = bpy.data.images.new("corpus", w, h, alpha=True, float_buffer=True)
    img.pixels = px
    d = ["Images", "Blender"]

    # Every codec this Blender offers; the ones the decoder does not read (PXR24, DWAA, DWAB, HTJ2K) must be refused.
    codecs = [c.identifier for c in bpy.types.ImageFormatSettings.bl_rna.properties["exr_codec"].enum_items]
    for codec in codecs:
        for depth, name in [("16", "half"), ("32", "float")]:
            for mode in ["RGB", "RGBA"]:
                try:
                    settings(scene, "OPEN_EXR", exr_codec=codec, color_depth=depth, color_mode=mode)
                except TypeError:
                    # Defined but not offered by this build.
                    continue
                img.save_render(out(*d, "exr_%s_%s_%s.exr" % (codec.lower(), name, mode.lower())), scene=scene)

    settings(scene, "HDR", color_mode="RGB")
    img.save_render(out(*d, "hdr_rgbe.hdr"), scene=scene)

    for depth in ["8", "16"]:
        for codec in ["NONE", "DEFLATE", "LZW", "PACKBITS"]:
            settings(scene, "TIFF", color_depth=depth, color_mode="RGBA", tiff_codec=codec)
            img.save_render(out(*d, "tiff_rgba%s_%s.tif" % (depth, codec.lower())), scene=scene)
    settings(scene, "PNG", color_depth="16", color_mode="RGBA", compression=50)
    img.save_render(out(*d, "png_rgba16.png"), scene=scene)
    settings(scene, "PNG", color_depth="16", color_mode="RGB", compression=90)
    img.save_render(out(*d, "png_rgb16.png"), scene=scene)
    settings(scene, "PNG", color_depth="8", color_mode="RGBA", compression=15)
    img.save_render(out(*d, "png_rgba8.png"), scene=scene)
    settings(scene, "TARGA", color_mode="RGBA")
    img.save_render(out(*d, "tga_rle_rgba.tga"), scene=scene)
    settings(scene, "TARGA_RAW", color_mode="RGB")
    img.save_render(out(*d, "tga_raw_rgb.tga"), scene=scene)
    settings(scene, "BMP", color_mode="RGB")
    img.save_render(out(*d, "bmp_rgb.bmp"), scene=scene)
    settings(scene, "JPEG", color_mode="RGB", quality=90)
    img.save_render(out(*d, "jpeg_q90.jpg"), scene=scene)


KHRONOS = os.path.join(ASSETS, "Models", "Khronos")
POLYHAVEN = os.path.join(ASSETS, "Models", "PolyHaven", "wooden_crate_02")

# (name, file, importer)
MODELS = [
    ("SimpleSkin", os.path.join(KHRONOS, "SimpleSkin", "glTF", "SimpleSkin.gltf"), "gltf"),
    ("AnimatedMorphCube", os.path.join(KHRONOS, "AnimatedMorphCube", "glTF-Binary", "AnimatedMorphCube.glb"), "gltf"),
    ("InterpolationTest", os.path.join(KHRONOS, "InterpolationTest", "glTF-Binary", "InterpolationTest.glb"), "gltf"),
    ("BoxVertexColors", os.path.join(KHRONOS, "BoxVertexColors", "glTF-Binary", "BoxVertexColors.glb"), "gltf"),
    ("Suzanne", os.path.join(KHRONOS, "Suzanne", "glTF", "Suzanne.gltf"), "gltf"),
    ("NormalTangentTest", os.path.join(KHRONOS, "NormalTangentTest", "glTF-Binary", "NormalTangentTest.glb"), "gltf"),
    ("Cameras", os.path.join(KHRONOS, "Cameras", "glTF", "Cameras.gltf"), "gltf"),
    ("DirectionalLight", os.path.join(KHRONOS, "DirectionalLight", "glTF-Binary", "DirectionalLight.glb"), "gltf"),
    ("TextureCoordinateTest", os.path.join(KHRONOS, "TextureCoordinateTest", "glTF-Binary", "TextureCoordinateTest.glb"), "gltf"),
    ("CrateFromGltf", os.path.join(POLYHAVEN, "glTF", "wooden_crate_02_1k.gltf"), "gltf"),
    ("CrateFromFbx", os.path.join(POLYHAVEN, "FBX", "wooden_crate_02_1k.fbx"), "fbx"),
]


def gltf_export(filepath, fmt, images):
    kw = dict(filepath=filepath, export_format=fmt, export_animations=True, export_morph=True, export_skins=True, export_cameras=True,
              export_lights=True)
    for option in (["export_image_format"] if not images else []):
        try:
            bpy.ops.export_scene.gltf(**kw, **{option: "NONE"})
            return
        except TypeError:
            pass
    bpy.ops.export_scene.gltf(**kw)


def models():
    for name, src, importer in MODELS:
        reset()
        if importer == "gltf":
            bpy.ops.import_scene.gltf(filepath=src)
        else:
            bpy.ops.import_scene.fbx(filepath=src)
        d = ["Models", "Blender", name]
        bpy.ops.export_scene.fbx(filepath=out(*d, name + ".fbx"), path_mode="STRIP", bake_anim=True, add_leaf_bones=False)
        bpy.ops.wm.obj_export(filepath=out(*d, name + ".obj"), export_materials=True, path_mode="STRIP")
        gltf_export(out(*d, name + ".glb"), "GLB", False)
        if name in ("Suzanne", "NormalTangentTest", "CrateFromGltf"):
            # Blender's own MikkTSpace tangents, for the tangent comparison. Vertices at one position are merged first:
            # MikkTSpace in Blender joins corners of one Blender vertex only, and imported files may repeat vertices.
            for o in [o for o in bpy.context.scene.objects if o.type == "MESH"]:
                bpy.context.view_layer.objects.active = o
                bpy.ops.object.mode_set(mode="EDIT")
                bpy.ops.mesh.select_all(action="SELECT")
                bpy.ops.mesh.remove_doubles(threshold=0.0)
                bpy.ops.object.mode_set(mode="OBJECT")
            bpy.ops.export_scene.gltf(filepath=out(*d, name + "_tangents.glb"), export_format="GLB", export_tangents=True, export_animations=False)
        if name in ("SimpleSkin", "BoxVertexColors"):
            gltf_export(out(*d, "gltf", name + ".gltf"), "GLTF_SEPARATE", False)
        if name == "TextureCoordinateTest":
            # Textures embedded in the FBX (video content), and the GLB with its images.
            bpy.ops.export_scene.fbx(filepath=out(*d, name + "_embedded.fbx"), path_mode="COPY", embed_textures=True, bake_anim=False)
            gltf_export(out(*d, name + "_images.glb"), "GLB", True)
        if name == "Suzanne":
            bpy.ops.wm.obj_export(filepath=out(*d, name + "_triangles.obj"), export_materials=False, export_triangulated_mesh=True,
                                  export_normals=False, export_uv=False)
            bpy.ops.export_scene.fbx(filepath=out(*d, name + "_cm_yup.fbx"), path_mode="STRIP", global_scale=100, axis_forward="Z", axis_up="Y",
                                     apply_unit_scale=False)


images()
models()
