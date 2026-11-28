#pragma once
#include <juce_core/juce_core.h>

namespace ce::gameplay
{
class WorldSession;
struct ClientSettings;
}
namespace ce::runtime { struct RuntimeGameSource; }

juce::String GameplayOption(const juce::ArgumentList& arguments, const juce::String& name);
bool ReadClientOptions(const juce::ArgumentList& arguments, ce::gameplay::ClientSettings& settings, juce::String& error);
ce::runtime::RuntimeGameSource AuthoredGameplayFixture(const juce::File& root);
void InstallGameplaySignalHandlers();
bool GameplayStopping();
int RunGameplayClientProbe(const juce::ArgumentList& arguments);
int RunLocalGameplayProbe(ce::gameplay::WorldSession& world, const juce::ArgumentList& arguments, int ticks);
