#pragma once

#include <optional>
#include <vector>

#include <JuceHeader.h>

#include "engine/world.h"
#include "Render/Scene/Animation.h"
#include "Scene/AnimationSlicer.h"

namespace ce {
class ViewportComponent;
}

namespace ce::scene {
class AssetCatalog;
}

namespace creation::assets {
class VirtualFileSystem;
class ProjectSession;
struct AssetDescriptor;
}

namespace ce::audio {
class AudioCatalog;
}

namespace ce::import {

// AI6: animation import configuration -- read by GltfAssetImporter only
// (other importers have no animation data and ignore this field
// entirely). Set by ImportPanel from its "Animation Import Options"
// section immediately before each filesDropped() call, not persisted
// per source file -- these are import-TIME settings, like "which
// codec," not properties of any one asset.
struct AnimationImportOptions {
    std::optional<AnimationInterpolation> interpolationOverride; // nullopt = keep each channel's authored interpolation.
    bool extractRootMotion = false;                              // strip translation off the skeleton's root joint (see AnimationSlicer::ExtractRootMotion).
    std::vector<scene::ClipSlice> slices;                        // empty = keep the single auto-extracted clip, unsliced.
};

// Everything a concrete AssetImporter might need to do its job. Not
// every importer needs every field -- an audio importer has no use for
// viewport, a glTF importer has no use for an audio device -- so these
// are pointers set up front by whoever owns the ImporterRegistry, rather
// than a constructor argument list that would force every future
// importer to know about every other importer's dependencies. Concrete
// importers null-check whatever they actually use.
struct ImportContext {
    engine::World* world = nullptr;
    scene::AssetCatalog* catalog = nullptr;
    creation::assets::VirtualFileSystem* vfs = nullptr;
    creation::assets::ProjectSession* projectSession = nullptr;
    juce::String gameAssetRoot;
    ViewportComponent* viewport = nullptr; // for RunOnGLThread -- see ViewportComponent.h.
    audio::AudioCatalog* audioCatalog = nullptr;
    juce::AudioFormatManager* audioFormatManager = nullptr;
    AnimationImportOptions animationOptions;

    // The Import Hub's first-import metadata popup's fields (Suite-Asset-
    // Pipeline-Model.md, Phase 3), read once per file immediately before
    // that file's Import() call and cleared immediately after -- NOT
    // sticky across files in a multi-file drop. pendingDisplayName empty
    // means "no override, derive the usual way" (every importer already
    // falls back to the source file's own name), same convention
    // ProjectContentAssetStore::importSource's new override params use.
    juce::String pendingDisplayName;
    juce::String pendingDescription;
    juce::StringArray pendingTags;
};

struct ImportResult {
    bool success = false;
    juce::String message; // human-readable status (success) or reason (failure) -- always set.

    static ImportResult Ok(juce::String msg) { return { true, std::move(msg) }; }
    static ImportResult Failed(juce::String msg) { return { false, std::move(msg) }; }
};

// One file format's import logic. Concrete importers (glTF, WAV, PNG,
// ...) each implement this and get handed to ImporterRegistry::Register
// -- the registry picks the right one for a dropped file by extension,
// so adding a new format never has to touch the hub UI, the registry, or
// any other importer.
class AssetImporter {
public:
    virtual ~AssetImporter() = default;

    // Shown in the Import Hub's staging list, e.g. "glTF Model".
    virtual juce::String DisplayName() const = 0;

    // Lowercase, no leading dot, e.g. {"gltf", "glb"}.
    virtual std::vector<juce::String> SupportedExtensions() const = 0;

    // Whether the Import Hub should show the first-import metadata popup
    // (name/description/tags) before calling Import() for this format.
    // True by default; TextureAssetImporter overrides this to false per
    // the project's own call that a plain image import needs no user
    // input. Importers that do want it read the result back off
    // ImportContext::pendingDisplayName/pendingDescription/pendingTags.
    virtual bool NeedsImportMetadata() const { return true; }

    virtual ImportResult Import(const juce::File& sourceFile, ImportContext& context) = 0;

    // Reimport / Update (Suite-Asset-Pipeline-Model.md, Phase 4): re-reads
    // sourceFile (existingAsset's own externalSourcePath, or a relocated
    // path the caller found via a file-browse dialog if the original went
    // missing) and updates existingAsset IN PLACE -- a new version of the
    // SAME asset id via ProjectAssetService::createNewVersion, and the
    // SAME runtime cache slot (keyed by existingAsset.displayName)
    // overwritten with the new content. Deliberately does NOT place a new
    // scene entity the way Import() does -- reimport updates what's
    // already there, it doesn't add another one.
    virtual ImportResult Reimport(const juce::File& sourceFile, const creation::assets::AssetDescriptor& existingAsset,
                                   ImportContext& context) = 0;
};

} // namespace ce::import
