#include "Import/Importers/GltfAssetImporter.h"

#include <creation/assets/ProjectAssetService.h>

#include "Assets/ProjectContentAssetStore.h"
#include "Diagnostics/EngineLog.h"
#include "Render/Import/GltfLoader.h"
#include "Render/ViewportComponent.h"
#include "Scene/AnimationSlicer.h"
#include "Scene/AssetCatalog.h"
#include "Scene/Components.h"
#include "Scene/ObjectDefinitionNaming.h"
#include "Scene/ObjectDefinitions.h"

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

// A source file with more than one mesh-bearing node decomposes into one
// Object Definition, one Mesh-kind component per mesh-bearing node -- see
// docs/OBJECT_MODEL.md's "Multi-part import decomposes into components".
// A single-mesh (or zero-mesh) file is a silent no-op (returns an empty
// string): no definition is created, exactly today's pre-multi-part
// behavior. `meshAssetId` is the asset's DURABLE id (stable across
// Reimport's version bumps), not its version -- so re-detecting the same
// asset on Reimport finds and updates the same definition rather than
// creating a duplicate. A failure here does NOT fail the whole import --
// the durable render asset itself is already safely stored either way by
// the time this runs -- but IS returned as text so it lands in the
// importer's own result message instead of disappearing silently (this
// used to only go to std::cout, which a windowed app with no attached
// console never shows anyone).
juce::String MaybeBuildMultiPartDefinition(scene::ObjectDefinitionCatalog& catalog, creation::assets::ProjectSession& session,
                                           const LoadedModel& model, const juce::String& meshAssetId,
                                           const juce::String& meshAssetVersionId, const juce::String& displayName) {
    std::vector<int> meshNodeIndices;
    for (std::size_t i = 0; i < model.nodes.size(); ++i) {
        if (model.nodes[i].meshIndex >= 0) meshNodeIndices.push_back(static_cast<int>(i));
    }
    if (meshNodeIndices.size() <= 1) {
        diagnostics::EngineLog::Info("Import", displayName + ": " + juce::String(static_cast<int>(meshNodeIndices.size())) +
                                                    " mesh-bearing node(s) -- single-mesh, no Object Definition needed.");
        return {};
    }

    scene::ObjectDefinition definition;
    const auto existingId = scene::FindWrapperDefinitionForRenderAsset(catalog, meshAssetId);
    definition.id = existingId.isNotEmpty() ? existingId : scene::GenerateWrapperDefinitionName(catalog, displayName);
    definition.displayName = displayName;

    for (const int nodeIndex : meshNodeIndices) {
        // Walk this node's parent chain up to the file's own root, then
        // compose top-down -- meshLocalTransform ends up relative to the
        // file's root, matching what instantiateDefinition composes it
        // against (the definition root entity's own transform).
        std::vector<int> chain;
        for (int idx = nodeIndex; idx != -1; idx = model.nodes[static_cast<std::size_t>(idx)].parentIndex) {
            chain.push_back(idx);
        }
        std::reverse(chain.begin(), chain.end());

        engine::Transform composed;
        for (const int idx : chain) {
            const auto& node = model.nodes[static_cast<std::size_t>(idx)];
            engine::Transform local;
            local.position = { node.localTranslation.x, node.localTranslation.y, node.localTranslation.z };
            local.eulerRotationRadians = { node.localEulerRotationRadians.x, node.localEulerRotationRadians.y,
                                           node.localEulerRotationRadians.z };
            local.scale = { node.localScale.x, node.localScale.y, node.localScale.z };
            composed = scene::composeTransform(composed, local);
        }

        scene::ObjectComponentEntry component;
        component.kind = scene::ObjectComponentKind::Mesh;
        component.meshAssetId = meshAssetId;
        component.meshAssetVersionId = meshAssetVersionId;
        component.meshNodeIndex = nodeIndex;
        component.meshNodeName = model.nodes[static_cast<std::size_t>(nodeIndex)].name;
        component.meshLocalTransform = composed;
        definition.components.push_back(std::move(component));
    }

    juce::String upsertError;
    if (!catalog.upsert(definition, upsertError)) {
        diagnostics::EngineLog::Error("Import", "Could not build multi-part Object Definition for " + displayName + ": " +
                                                     upsertError);
        return " Could not build its multi-part Object Definition: " + upsertError;
    }
    juce::String saveError;
    if (!catalog.Save(session, definition.id, saveError)) {
        diagnostics::EngineLog::Error("Import", "Built Object Definition \"" + definition.id + "\" for " + displayName +
                                                     " but could not save it: " + saveError);
        return " Built Object Definition \"" + definition.id + "\" but could not save it: " + saveError;
    }
    diagnostics::EngineLog::Info("Import", "Created Object Definition \"" + definition.id + "\" (" +
                                                juce::String(static_cast<int>(meshNodeIndices.size())) + " parts) for " +
                                                displayName + ".");
    return " Also created Object Definition \"" + definition.id + "\" (" +
           juce::String(static_cast<int>(meshNodeIndices.size())) + " parts).";
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

    juce::String objectDefinitionNote;
    if (context.objectDefinitions != nullptr) {
        objectDefinitionNote = MaybeBuildMultiPartDefinition(*context.objectDefinitions, *context.projectSession, model,
                                                             sourceDescriptor.id, sourceDescriptor.versionId, assetName);
    }

    // Import only stages the asset in the catalog -- it does NOT place an
    // instance in the scene. Placing is a separate, deliberate action
    // (Hierarchy's "+ Add", or Content Browser once models get the same
    // place-from-catalog treatment as Pods/Object Definitions).
    auto result = ImportResult::Ok(assetName + " stored in project content." + animationNote + objectDefinitionNote);
    result.createdAssetId = sourceDescriptor.id;
    return result;
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

    juce::String objectDefinitionNote;
    if (context.objectDefinitions != nullptr) {
        objectDefinitionNote = MaybeBuildMultiPartDefinition(*context.objectDefinitions, *context.projectSession, model,
                                                             newDescriptor.id, newDescriptor.versionId, existingAsset.displayName);
    }

    return ImportResult::Ok(existingAsset.displayName + " updated to a new version." + animationNote + objectDefinitionNote);
}

} // namespace ce::import
