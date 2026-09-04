#pragma once

#include <vector>

#include <JuceHeader.h>

namespace ce::project {

// One choice in the New Game picker. Plain data, no UI dependency -- an
// open-ended std::vector rather than a hardcoded enum/switch, so a future
// game type is a one-line addition here, never a dialog code change (the
// user's own requirement: "leave some additional slots because we may
// have various game types we want to support").
struct StarterGameTemplate {
    juce::String id;                     // stable key, e.g. "welcome-yard".
    juce::String displayName;            // "Welcome Yard".
    juce::String description;            // one-line blurb shown in the picker.
    juce::String starterSceneTemplateId; // pack.json scene id, via EngineAssetPack::readScene(). "DefaultScene" = empty geometry-free scene.
    juce::String starterModelAssetId;    // pack.json model id to import and place at scene origin; empty = no placement.
};

inline std::vector<StarterGameTemplate> GetStarterGameTemplates() {
    return {
        { "welcome-yard", "Welcome Yard", "A small yard with a house, road, and lamps -- a general-purpose starting point.",
          "DefaultScene", "01_Welcome_Yard" },
        { "material-lab", "Material Lab", "A row of material sample panels -- good for testing lighting/materials.",
          "DefaultScene", "02_Material_Lab" },
        { "traversal-yard", "Traversal Yard", "Platforms and a ramp at varying heights -- a starting point for movement/platforming.",
          "DefaultScene", "03_Traversal_Yard" },
        { "village-block", "Village Block", "Two houses along a path with garden beds -- a small settlement starting point.",
          "DefaultScene", "04_Village_Block" },
        { "editor-garage", "Editor Garage", "A workbench and monitor wall -- a starting point for tool/editor-style games.",
          "DefaultScene", "05_Editor_Garage" },
        { "animation-corral", "Animation Corral", "A fenced yard with scale-reference mannequin and animals -- a starting point for animation/character work.",
          "DefaultScene", "06_Animation_Corral" },
        // Empty entries here are exactly how a future game type gets added --
        // one more line, no dialog code changes.
        { "empty-scene", "Empty Scene", "Just the default empty scene, no starter content placed.", "DefaultScene", "" },
    };
}

} // namespace ce::project
