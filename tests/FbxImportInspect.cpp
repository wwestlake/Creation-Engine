// Manual diagnostic CLI: LoadFbx() a real file and print exactly what was
// extracted (mesh/primitive counts, skeleton, materials, animation clips).
// Not a ctest -- it needs a real external FBX file, not a repo fixture --
// but a real, permanent, non-hardcoded-path tool (usage: pass the .fbx path
// as argv[1]) for verifying an import without going through the editor UI.
#include "Render/Import/FbxLoader.h"

#include <cstdio>

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("usage: FbxImportInspect <path-to.fbx>\n");
        return 1;
    }

    ce::LoadedModel model;
    juce::String error;
    const juce::File file{ juce::String(argv[1]) };
    if (! ce::LoadFbx(file, model, error)) {
        std::printf("FAILED: %s\n", error.toRawUTF8());
        return 1;
    }

    std::printf("OK: %s\n", file.getFileName().toRawUTF8());
    std::printf("  meshes (nodes with a mesh): %d\n", (int) model.meshPrimitiveRanges.size());
    std::printf("  primitives (mesh x material parts): %d\n", (int) model.primitives.size());
    for (std::size_t i = 0; i < model.primitives.size(); ++i) {
        const auto& p = model.primitives[i];
        std::printf("    primitive %d: %d vertices, %d indices, materialIndex=%d\n", (int) i,
                    (int) p.vertices.size(), (int) p.indices.size(), p.materialIndex);
    }
    std::printf("  materials: %d\n", (int) model.materials.size());
    for (std::size_t i = 0; i < model.materials.size(); ++i) {
        const auto& m = model.materials[i];
        std::printf("    material %d: baseColor=(%.2f,%.2f,%.2f) hasBaseColorTexture=%s hasNormalTexture=%s\n",
                    (int) i, m.baseColorFactor.x, m.baseColorFactor.y, m.baseColorFactor.z,
                    (m.baseColorTexturePath.existsAsFile() || m.baseColorTextureBytes.getSize() > 0) ? "yes" : "no",
                    (m.normalTexturePath.existsAsFile() || m.normalTextureBytes.getSize() > 0) ? "yes" : "no");
    }
    if (model.skin.has_value()) {
        std::printf("  skeleton: %d joints\n", (int) model.skin->joints.size());
        for (const auto& joint : model.skin->joints) {
            std::printf("    joint: %s (parent=%d)\n", joint.name.toRawUTF8(), joint.parentIndex);
        }
    } else {
        std::printf("  skeleton: none\n");
    }
    std::printf("  animation clips: %d\n", (int) model.animations.size());
    for (const auto& clip : model.animations) {
        std::printf("    clip '%s': %.3fs, %d channels\n", clip.name.toRawUTF8(), clip.duration,
                    (int) clip.channels.size());
    }

    return 0;
}
