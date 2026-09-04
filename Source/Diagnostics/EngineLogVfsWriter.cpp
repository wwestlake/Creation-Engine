#include "Diagnostics/EngineLogVfsWriter.h"

#include <creation/assets/ProjectSession.h>

#include "Diagnostics/EngineLog.h"

namespace ce::diagnostics {

namespace {
const juce::String kLogicalPath = "Logs/engine.log";
}

EngineLogVfsWriter::~EngineLogVfsWriter() {
    stopTimer();
}

void EngineLogVfsWriter::SetSession(creation::assets::ProjectSession* session) {
    if (session_ != nullptr) FlushNow(); // don't drop what's pending against the outgoing project.
    session_ = session;
    if (session_ != nullptr) startTimer(kFlushIntervalMs);
    else stopTimer();
}

void EngineLogVfsWriter::timerCallback() {
    FlushNow();
}

void EngineLogVfsWriter::FlushNow() {
    if (session_ == nullptr || !session_->isValid()) return;

    const auto newEntries = EngineLog::Instance().ConsumeSince(cursor_);
    if (newEntries.empty()) return;

    juce::MemoryBlock existing;
    juce::String combined;
    if (session_->readEntry(kLogicalPath, existing)) {
        combined = existing.toString();
    }
    for (const auto& entry : newEntries) combined << entry.ToLine() << juce::newLine;

    // Simple size-based rotation -- keeps this file self-bounded across a
    // long session instead of growing forever. Trims whole lines off the
    // front so what remains is always valid log text, never a mid-line
    // fragment.
    if (combined.getNumBytesAsUTF8() > static_cast<std::size_t>(kMaxLogFileBytes)) {
        const auto targetBytes = static_cast<std::size_t>(kMaxLogFileBytes) * 3 / 4;
        while (combined.getNumBytesAsUTF8() > targetBytes) {
            const auto nextLine = combined.indexOfChar('\n');
            if (nextLine < 0) break;
            combined = combined.substring(nextLine + 1);
        }
    }

    const juce::MemoryBlock data(combined.toRawUTF8(), combined.getNumBytesAsUTF8());
    session_->writeEntry(kLogicalPath, data);
}

} // namespace ce::diagnostics
