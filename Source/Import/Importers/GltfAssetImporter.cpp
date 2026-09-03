#include "Import/Importers/GltfAssetImporter.h"

#include <creation/assets/ProjectAssetService.h>

#include "Assets/ProjectContentAssetStore.h"
#include "Render/Import/GltfLoader.h"
#include "Render/ViewportComponent.h"
#include "Scene/AnimationSlicer.h"
#include "Scene/AssetCatalog.h"
#include "Scene/Components.h"

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
    if (context.catalog == nullptr || context.viewport == nullptr || context.world == nullptr || context.projectSession == nullptr) {
        return ImportResult::Failed("glTF import needs an open game, world, asset catalog, and viewport.");
    }

    LoadedModel model;
    if (!LoadGltf(sourceFile, model) || model.primitives.empty()) {
        return ImportResult::Failed("Failed to parse " + sourceFile.getFileName() + " (see log for details).");
    }

    juce::String animationNote;
    if (!model.animations.empty()) {
        ApplyAnimationImportOptions(model, context.animationOptions, animationNote);
    }

    juce::StringArray tags{ "model", "gltf" };
    tags.addArray(context.pendingTags);

    creation::assets::AssetDescriptor sourceDescriptor;
    juce::String persistenceError;
    if (! assets::ProjectContentAssetStore::importSource(*context.projectSession, context.gameAssetRoot, sourceFile,
                                                          creation::assets::AssetKind::render, "Model", tags,
                                                          sourceDescriptor, persistenceError,
                                                          context.pendingDisplayName, context.pendingDescription))
        return ImportResult::Failed("Could not store glTF source in project content: " + persistenceError);

    const juce::String assetName =
        context.pendingDisplayName.isNotEmpty() ? context.pendingDisplayName : sourceFile.getFileNameWithoutExtension();

    bool added = false;
    context.viewport->RunOnGLThread(
        [&] { added = context.catalog->AddFromModel(assetName, model); }, /*blockUntilFinished=*/true);

    if (!added) {
        return ImportResult::Failed("Failed to build GPU resources for " + sourceFile.getFileName() + ".");
    }
    if (! context.catalog->SetSourceIdentity(assetName, sourceDescriptor.id, sourceDescriptor.versionId) ||
        ! context.catalog->AddAlias(sourceDescriptor.id, assetName))
        return ImportResult::Failed("The imported model could not be registered with its project asset identity.");

    // Import only stages the asset in the catalog -- it does NOT place an
    // instance in the scene. Placing is a separate, deliberate action
    // (Hierarchy's "+ Add", or Content Browser once models get the same
    // place-from-catalog treatment as Pods/Object Definitions).
    return ImportResult::Ok(assetName + " stored in project content." + animationNote);
}

ImportResult GltfAssetImporter::Reimport(const juce::File& sourceFile,
                                         const creation::assets::AssetDescriptor& existingAsset,
                                         ImportContext& context) {
    if (context.catalog == nullptr || context.viewport == nullptr || context.projectSession == nullptr) {
        return ImportResult::Failed("glTF reimport needs an open game, asset catalog, and viewport.");
    }

    LoadedModel model;
    if (!LoadGltf(sourceFile, model) || model.primitives.empty()) {
        return ImportResult::Failed("Failed to parse " + sourceFile.getFileName() + " (see log for details).");
    }

    juce::String animationNote;
    if (!model.animations.empty()) {
        ApplyAnimationImportOptions(model, context.animationOptions, animationNote);
    }

    creation::assets::ProjectAssetService::ImportOptions overrides; // left blank -- createNewVersion falls back to existingAsset's own fields.
    creation::assets::AssetDescriptor newDescriptor;
    juce::String persistenceError;
    if (! creation::assets::ProjectAssetService::createNewVersion(*context.projectSession, existingAsset, sourceFile,
                                                                   overrides, newDescriptor, persistenceError))
        return ImportResult::Failed("Could not save the new version: " + persistenceError);
    if (! context.projectSession->commit(persistenceError))
        return ImportResult::Failed("New version saved, but the project manifest could not be committed: " + persistenceError);

    bool added = false;
    context.viewport->RunOnGLThread(
        [&] { added = context.catalog->AddFromModel(existingAsset.displayName, model); }, /*blockUntilFinished=*/true);
    if (!added) {
        return ImportResult::Failed("Failed to rebuild GPU resources for " + sourceFile.getFileName() + ".");
    }
    if (! context.catalog->SetSourceIdentity(existingAsset.displayName, newDescriptor.id, newDescriptor.versionId) ||
        ! context.catalog->AddAlias(newDescriptor.id, existingAsset.displayName))
        return ImportResult::Failed("The reimported model could not be re-registered with its project asset identity.");

    return ImportResult::Ok(existingAsset.displayName + " updated to a new version." + animationNote);
}

} // namespace ce::import
