#include "Diagnostics/EngineLog.h"

namespace ce::diagnostics {

juce::String ToString(LogLevel level) {
    switch (level) {
        case LogLevel::Trace: return "TRACE";
        case LogLevel::Debug: return "DEBUG";
        case LogLevel::Info: return "INFO ";
        case LogLevel::Warning: return "WARN ";
        case LogLevel::Error: return "ERROR";
        case LogLevel::Critical: return "CRIT ";
    }
    return "INFO ";
}

juce::Colour ColourFor(LogLevel level) {
    switch (level) {
        case LogLevel::Trace: return juce::Colour(0xff5a6270);
        case LogLevel::Debug: return juce::Colour(0xff7a90b0);
        case LogLevel::Info: return juce::Colours::white;
        case LogLevel::Warning: return juce::Colour(0xffffb454);
        case LogLevel::Error: return juce::Colour(0xffff6b6b);
        case LogLevel::Critical: return juce::Colour(0xffff3b3b);
    }
    return juce::Colours::white;
}

juce::String LogEntry::ToLine() const {
    return timestamp.formatted("%Y-%m-%d %H:%M:%S") + "." +
           juce::String(timestamp.toMilliseconds() % 1000).paddedLeft('0', 3) + " [" + ToString(level) + "] [" +
           category + "] " + message;
}

EngineLog& EngineLog::Instance() {
    static EngineLog instance;
    return instance;
}

void EngineLog::Write(LogLevel level, const juce::String& category, const juce::String& message) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        entries_.push_back(LogEntry{ juce::Time::getCurrentTime(), level, category, message });
        ++totalEverWritten_;
        while (entries_.size() > kMaxEntries) entries_.pop_front();
    }

    if (onNewEntry) {
        juce::MessageManager::callAsync([this] {
            if (onNewEntry) onNewEntry();
        });
    }
}

void EngineLog::Trace(const juce::String& category, const juce::String& message) {
    Instance().Write(LogLevel::Trace, category, message);
}
void EngineLog::Debug(const juce::String& category, const juce::String& message) {
    Instance().Write(LogLevel::Debug, category, message);
}
void EngineLog::Info(const juce::String& category, const juce::String& message) {
    Instance().Write(LogLevel::Info, category, message);
}
void EngineLog::Warning(const juce::String& category, const juce::String& message) {
    Instance().Write(LogLevel::Warning, category, message);
}
void EngineLog::Error(const juce::String& category, const juce::String& message) {
    Instance().Write(LogLevel::Error, category, message);
}
void EngineLog::Critical(const juce::String& category, const juce::String& message) {
    Instance().Write(LogLevel::Critical, category, message);
}

std::vector<LogEntry> EngineLog::Snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return { entries_.begin(), entries_.end() };
}

std::vector<LogEntry> EngineLog::ConsumeSince(std::size_t& cursor) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const std::size_t oldestAvailable = totalEverWritten_ - entries_.size();
    // Entries older than the buffer's current tail were evicted before
    // this caller got to them -- accept the gap (this is a diagnostic
    // convenience, not a durability guarantee) rather than fail; a 5000-
    // entry buffer drained every few seconds by EngineLogVfsWriter should
    // never realistically hit this in practice.
    const std::size_t startAbsolute = cursor < oldestAvailable ? oldestAvailable : cursor;
    const std::size_t startOffset = startAbsolute - oldestAvailable;

    std::vector<LogEntry> result;
    if (startOffset < entries_.size()) {
        result.assign(entries_.begin() + static_cast<std::ptrdiff_t>(startOffset), entries_.end());
    }
    cursor = totalEverWritten_;
    return result;
}

void EngineLog::Clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    entries_.clear();
}

} // namespace ce::diagnostics
