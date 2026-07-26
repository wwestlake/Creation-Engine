#include "Import/Importers/GltfAssetImporter.h"

#include "Render/Import/GltfLoader.h"
#include "Render/ViewportComponent.h"
#include "Scene/AnimationSlicer.h"
#include "Scene/AssetCatalog.h"

namespace ce::import {

namespace {

// Mutates model.animations in place per the Import Hub's current
// Animation Import Options, appending a human-readable summary of what
// happened to outNote. A no-op (silently) for every option the caller
// left at its default -- an animated glTF dropped with no options set
// behaves exactly as it did before AI6.
void ApplyAnimationImportOptions(LoadedModel& model, const AnimationImportOptions& options, juce::String& outNote) {
    if (options.interpolationOverride.has_value()) {
        for (auto& clip : model.animations) {
            for (auto& channel : clip.channels) {
                channel.interpolation = *options.interpolationOverride;
            }
        }
    }

    if (options.extractRootMotion && model.skin.has_value()) {
        // The skeleton root is whichever joint has no parent within the
        // skin -- no UI joint-picker for this yet (see AssetImporter.h's
        // AnimationImportOptions doc), which is fine for the common
        // single-root-joint case every skinned asset seen so far has.
        int rootJointIndex = -1;
        for (std::size_t i = 0; i < model.skin->joints.size(); ++i) {
            if (model.skin->joints[i].parentIndex == -1) {
                rootJointIndex = static_cast<int>(i);
                break;
            }
        }

        if (rootJointIndex >= 0) {
            int extractedCount = 0;
            for (auto& clip : model.animations) {
                if (scene::ExtractRootMotion(clip, rootJointIndex)) {
                    ++extractedCount;
                }
            }
            if (extractedCount > 0) {
                outNote += " Root motion extracted from " + juce::String(extractedCount) + " clip(s).";
            }
        }
    }

    if (!options.slices.empty()) {
        std::vector<AnimationClip> slicedClips;
        for (const auto& clip : model.animations) {
            auto sliced = scene::SliceClip(clip, options.slices);
            slicedClips.insert(slicedClips.end(), std::make_move_iterator(sliced.begin()),
                                std::make_move_iterator(sliced.end()));
        }
        if (!slicedClips.empty()) {
            model.animations = std::move(slicedClips);
            outNote += " Sliced into " + juce::String(static_cast<int>(model.animations.size())) + " named clip(s).";
        }
    }
}

} // namespace

ImportResult GltfAssetImporter::Import(const juce::File& sourceFile, ImportContext& context) {
    if (context.catalog == nullptr || context.viewport == nullptr) {
        return ImportResult::Failed("glTF import needs a live asset catalog and viewport.");
    }

    LoadedModel model;
    if (!LoadGltf(sourceFile, model) || model.primitives.empty()) {
        return ImportResult::Failed("Failed to parse " + sourceFile.getFileName() + " (see log for details).");
    }

    juce::String animationNote;
    if (!model.animations.empty()) {
        ApplyAnimationImportOptions(model, context.animationOptions, animationNote);
    }

    const juce::String assetName = sourceFile.getFileNameWithoutExtension();

    bool added = false;
    context.viewport->RunOnGLThread(
        [&] { added = context.catalog->AddFromModel(assetName, model); }, /*blockUntilFinished=*/true);

    if (!added) {
        return ImportResult::Failed("Failed to build GPU resources for " + sourceFile.getFileName() + ".");
    }
    return ImportResult::Ok(assetName + " imported -- available in the \"+ Add\" menu." + animationNote);
}

} // namespace ce::import
