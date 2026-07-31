#pragma once

#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>

#include "engine/world.h"
#include "Scene/Components.h"

namespace ce::scene
{
class EngineSceneSerializer final
{
public:
    static juce::ValueTree serializeScene(ce::engine::World& world);
    static bool restoreScene(ce::engine::World& world, const juce::ValueTree& state);
};
}
