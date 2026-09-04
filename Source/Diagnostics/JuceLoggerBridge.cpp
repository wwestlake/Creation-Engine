#include "Diagnostics/JuceLoggerBridge.h"

#include "Diagnostics/EngineLog.h"

namespace ce::diagnostics {

void JuceLoggerBridge::logMessage(const juce::String& message) {
    EngineLog::Info("General", message);
}

} // namespace ce::diagnostics
