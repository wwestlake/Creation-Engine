#pragma once

#include <JuceHeader.h>
#include "GameplayClient.h"

namespace ce::gameplay
{
std::unique_ptr<juce::DocumentWindow> OpenRemoteGame(const juce::ArgumentList& arguments, juce::String& error);
} // namespace ce::gameplay
