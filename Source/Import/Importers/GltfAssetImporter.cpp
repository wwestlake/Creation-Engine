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

// A source file's mesh-bearing nodes decompose into one Object Definition
// PER independent top-level node in the source, one Mesh-kind component
// per mesh-bearing node within each -- see docs/OBJECT_MODEL.md's
// "Multi-part import decomposes into components". Never invent a parent
// that doesn't exist in the source: two mesh nodes only ever land in the
// SAME definition if the file itself actually related them through a real
// parent chain. A Blender export with several independent top-level
// objects produces several independent definitions, not one shared
// container bundling unrelated objects together (a confirmed bug -- one
// invented "definition root" was silently composing every top-level
// node's own file-space offset as if they were meaningfully related,
// which they weren't).
//
// A node with no parent (the ultimate root of its own group) gets
// meshLocalTransform == identity, not its own authored local transform --
// that transform only ever meant "this object's position relative to
// its sibling objects in the file's own arbitrary space," which stops
// meaning anything once it's split out into its own independent
// definition. A node WITH a real parent keeps its transform relative to
// that parent exactly as authored -- genuine, meaningful relationship
// data, composed the same way as before.
//
// There is deliberately no special case for exactly one mesh-bearing
// node: a node's own local transform (relative to its REAL parent, if it
// has one) is authored data regardless of how many sibling nodes it has,
// and silently dropping it for a single-node file was a real, confirmed
// bug (a boulder asset with a nonzero node offset rendered at the wrong
// position because the old ">1 nodes" threshold skipped decomposition
// entirely for it). A file with genuinely zero mesh-bearing nodes is the
// only real no-op -- nothing exists to place. `meshAssetId` is the
// asset's DURABLE id (stable across Reimport's version bumps), not its
// version -- so re-detecting the same asset on Reimport finds and
// updates the same definition rather than creating a duplicate, for the
// common single-group case (see the NOTE below for the multi-group
// limitation). A failure on any one group does NOT fail the whole
// import, or stop the other groups -- the durable render asset itself is
// already safely stored either way by the time this runs.
juce::String BuildNodeDecomposedDefinitions(scene::ObjectDefinitionCatalog& catalog, creation::assets::ProjectSession& session,
                                            const LoadedModel& model, const juce::String& meshAssetId,
                                            const juce::String& meshAssetVersionId, const juce::String& displayName) {
    std::vector<int> meshNodeIndices;
    for (std::size_t i = 0; i < model.nodes.size(); ++i) {
        if (model.nodes[i].meshIndex >= 0) meshNodeIndices.push_back(static_cast<int>(i));
    }
    if (meshNodeIndices.empty()) {
        diagnostics::EngineLog::Info("Import", displayName + ": no mesh-bearing nodes -- no Object Definition needed.");
        return {};
    }

    // Ultimate root (walk parentIndex to -1) for each mesh-bearing node --
    // nodes sharing the same root are genuinely related in the source
    // file; nodes with different roots are independent siblings and MUST
    // become separate definitions, per this function's own comment above.
    const auto ultimateRoot = [&](int nodeIndex) {
        int idx = nodeIndex;
        while (model.nodes[static_cast<std::size_t>(idx)].parentIndex != -1)
            idx = model.nodes[static_cast<std::size_t>(idx)].parentIndex;
        return idx;
    };

    std::vector<int> rootOrder; // first-seen order, for stable naming/output.
    std::unordered_map<int, std::vector<int>> nodesByRoot;
    for (const int nodeIndex : meshNodeIndices) {
        const int root = ultimateRoot(nodeIndex);
        if (nodesByRoot.find(root) == nodesByRoot.end()) rootOrder.push_back(root);
        nodesByRoot[root].push_back(nodeIndex);
    }
    const bool singleGroup = rootOrder.size() == 1;

    juce::String combinedNote;
    for (std::size_t groupIndex = 0; groupIndex < rootOrder.size(); ++groupIndex) {
        const int rootNodeIndex = rootOrder[groupIndex];
        const auto& groupNodeIndices = nodesByRoot[rootNodeIndex];

        // The common case (one connected group spanning the whole file --
        // the only shape this function used to support) keeps today's
        // exact naming/reimport-identity behavior. Only a file with
        // genuinely multiple independent top-level nodes uses a per-root
        // name: the source node's own authored name (a Blender object's
        // real name), falling back to displayName + a 1-based index if
        // the file left that node unnamed.
        const juce::String groupDisplayName = singleGroup
            ? displayName
            : (model.nodes[static_cast<std::size_t>(rootNodeIndex)].name.isNotEmpty()
                   ? model.nodes[static_cast<std::size_t>(rootNodeIndex)].name
                   : displayName + " " + juce::String(static_cast<int>(groupIndex) + 1));

        scene::ObjectDefinition definition;
        // NOTE: FindWrapperDefinitionForRenderAsset matches by meshAssetId
        // alone, not by which specific nodes a definition contains -- exact
        // for the single-group case (only one candidate can ever exist),
        // but can't yet distinguish between several existing per-root
        // definitions for the same multi-root file on reimport. Falls back
        // to creating a fresh definition; GenerateWrapperDefinitionName's
        // own dedup-by-name still prevents an outright duplicate under the
        // exact same name. Real per-root reimport identity is real, but
        // separate, follow-on scope -- not something the multi-root case
        // needs to get exactly right on its very first pass.
        const auto existingId = scene::FindWrapperDefinitionForRenderAsset(catalog, meshAssetId);
        definition.id = (singleGroup && existingId.isNotEmpty())
            ? existingId : scene::GenerateWrapperDefinitionName(catalog, groupDisplayName);
        definition.displayName = groupDisplayName;

        for (const int nodeIndex : groupNodeIndices) {
            // Walk up to (but NOT including) this group's own root, then
            // compose top-down -- meshLocalTransform ends up relative to
            // the root's own space, matching what instantiateDefinition
            // composes it against (the definition root entity's own
            // transform). The root's own authored transform is
            // deliberately excluded (see this function's header comment)
            // -- for nodeIndex == rootNodeIndex, chain is empty and
            // composed stays identity.
            std::vector<int> chain;
            for (int idx = nodeIndex; idx != rootNodeIndex; idx = model.nodes[static_cast<std::size_t>(idx)].parentIndex) {
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
            diagnostics::EngineLog::Error("Import", "Could not build Object Definition for " + groupDisplayName + ": " + upsertError);
            combinedNote += " Could not build Object Definition \"" + groupDisplayName + "\": " + upsertError;
            continue;
        }
        juce::String saveError;
        if (!catalog.Save(session, definition.id, saveError)) {
            diagnostics::EngineLog::Error("Import", "Built Object Definition \"" + definition.id + "\" for " + groupDisplayName +
                                                         " but could not save it: " + saveError);
            combinedNote += " Built Object Definition \"" + definition.id + "\" but could not save it: " + saveError;
            continue;
        }
        diagnostics::EngineLog::Info("Import", "Created Object Definition \"" + definition.id + "\" (" +
                                                    juce::String(static_cast<int>(groupNodeIndices.size())) + " parts) for " +
                                                    groupDisplayName + ".");
        combinedNote += " Also created Object Definition \"" + definition.id + "\" (" +
               juce::String(static_cast<int>(groupNodeIndices.size())) + " parts).";
    }
    return combinedNote;
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
        objectDefinitionNote = BuildNodeDecomposedDefinitions(*context.objectDefinitions, *context.projectSession, model,
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
        objectDefinitionNote = BuildNodeDecomposedDefinitions(*context.objectDefinitions, *context.projectSession, model,
                                                             newDescriptor.id, newDescriptor.versionId, existingAsset.displayName);
    }

    return ImportResult::Ok(existingAsset.displayName + " updated to a new version." + animationNote + objectDefinitionNote);
}

} // namespace ce::import
