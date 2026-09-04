#pragma once

#include <JuceHeader.h>

#include "Scene/ObjectDefinitions.h"

namespace ce::scene {

// Same first-free-numbered-slot dedup shape as ContentBrowserPanel.cpp's
// GenerateDefaultObjectDefinitionName, seeded from a real name (a dropped
// mesh's display name, or an imported file's name) instead of the generic
// "New Object Definition" -- so the same source dragged or imported twice
// reuses one definition (see FindWrapperDefinitionForRenderAsset below)
// and a genuinely new name only takes this path once. Shared by
// MainComponent (drag-and-drop placement) and GltfAssetImporter
// (multi-part import) rather than forked between them.
juce::String GenerateWrapperDefinitionName(const ObjectDefinitionCatalog& catalog, const juce::String& baseName);

// Finds an existing Object Definition that's a pure wrapper for
// meshAssetId -- one or more Mesh-kind components, ALL referencing this
// asset, and nothing else -- so re-dropping the same raw mesh, or an
// importer re-detecting the same multi-part source on reimport, reuses
// one definition rather than creating a near-duplicate each time. A
// definition with any non-Mesh component (a Pod, a Child), or a Mesh
// component referencing a DIFFERENT asset, is deliberately NOT matched --
// reusing someone's hand-built, richer definition just because it shares
// a mesh would be a surprising, wrong guess.
juce::String FindWrapperDefinitionForRenderAsset(const ObjectDefinitionCatalog& catalog, const juce::String& meshAssetId);

} // namespace ce::scene
