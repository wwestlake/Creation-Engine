#pragma once

#include <JuceHeader.h>

#include "Scene/Components.h"

namespace ce::views
{
// Creates engine-defined scene entities. These are deliberately separate
// from Content Browser assets: a Player Start or trigger is scene structure,
// not a file a project imports or owns.
class SceneBuiltinsPanel final : public juce::Component
{
public:
    SceneBuiltinsPanel();

    std::function<void(scene::BuiltInKind)> onCreateBuiltIn;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    void addTile(juce::TextButton& button, scene::BuiltInKind kind, const juce::String& tooltip);

    juce::Label heading_{ {}, "Engine Built-ins" };
    juce::Label help_{ {}, "Create authored scene objects. These are not project assets." };
    juce::TextButton playerStart_{ "Player Start" };
    juce::TextButton spawner_{ "Spawner" };
    juce::TextButton cameraMarker_{ "Camera Marker" };
    juce::TextButton triggerVolume_{ "Trigger Volume" };
    juce::TextButton waypoint_{ "Waypoint" };
    juce::TextButton audioEmitter_{ "Audio Emitter" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SceneBuiltinsPanel)
};
} // namespace ce::views
