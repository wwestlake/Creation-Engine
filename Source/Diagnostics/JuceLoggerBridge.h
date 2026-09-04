#pragma once

#include <JuceHeader.h>

namespace ce::diagnostics {

// Installed once via juce::Logger::setCurrentLogger so every EXISTING
// juce::Logger::writeToLog(...) call already scattered across this
// codebase (ViewportComponent, MainComponent, ...) flows into EngineLog
// automatically, tagged category "General" -- without touching those call
// sites. New/updated call sites should prefer EngineLog::Info/Warning/
// Error(category, message) directly for a real category instead of
// relying on this bridge's generic one.
class JuceLoggerBridge final : public juce::Logger {
public:
    void logMessage(const juce::String& message) override;
};

} // namespace ce::diagnostics
