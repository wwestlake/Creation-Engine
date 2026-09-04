#pragma once

#include <deque>
#include <functional>
#include <mutex>
#include <vector>

#include <JuceHeader.h>

namespace ce::diagnostics {

// Deliberately small (5 levels, timestamp + category + message, no
// structured fields beyond that) -- a real logger the rest of the engine
// can instrument against today, not the final word on logging
// architecture. Expected to be migrated/expanded later; this is the
// professional-shape baseline (timestamp, level, category) requested now,
// not a rabbit hole.
enum class LogLevel { Trace, Debug, Info, Warning, Error, Critical };

juce::String ToString(LogLevel level);
juce::Colour ColourFor(LogLevel level);

struct LogEntry {
    juce::Time timestamp;
    LogLevel level = LogLevel::Info;
    juce::String category;
    juce::String message;

    // Fixed-column plain-text line, e.g.:
    // "2026-09-03 22:15:03.123 [INFO ] [Import] Rock_Scatter_Cluster stored in project content."
    // -- the exact format written to the VFS log file and shown in the Log
    // panel alike, so what you read on screen matches what's on disk.
    juce::String ToLine() const;
};

// Process-wide structured log: EngineLog::Info("Import", "...") etc. Safe
// to call from any thread (render thread, GL thread, message thread) --
// Write() takes a lock; onNewEntry is marshaled onto the message thread
// before firing, so a UI listener never has to think about thread safety
// either.
//
// Bounded ring buffer (kMaxEntries) for live viewing -- this is a runtime
// diagnostic aid, not the durable record; EngineLogVfsWriter is what makes
// entries durable (see its own header), and it drains the buffer via
// ConsumeSince faster than the buffer could realistically wrap for any
// sane flush interval.
class EngineLog final {
public:
    static EngineLog& Instance();

    void Write(LogLevel level, const juce::String& category, const juce::String& message);

    static void Trace(const juce::String& category, const juce::String& message);
    static void Debug(const juce::String& category, const juce::String& message);
    static void Info(const juce::String& category, const juce::String& message);
    static void Warning(const juce::String& category, const juce::String& message);
    static void Error(const juce::String& category, const juce::String& message);
    static void Critical(const juce::String& category, const juce::String& message);

    // Thread-safe snapshot, newest last -- copied out rather than handing
    // back a reference into the live deque, same reasoning
    // AssetCatalog::Find already documents for its own concurrent-reader
    // case (a concurrent Write() could otherwise invalidate what a viewer
    // is still iterating).
    [[nodiscard]] std::vector<LogEntry> Snapshot() const;

    // Entries written since `cursor` (an opaque position this function
    // both reads and advances) -- lets EngineLogVfsWriter pull only what's
    // new each flush instead of re-serializing the whole buffer every
    // time. `cursor` starts at 0 (meaning "from the beginning").
    [[nodiscard]] std::vector<LogEntry> ConsumeSince(std::size_t& cursor) const;

    void Clear();

    // Fired asynchronously (message thread) after every Write() -- a
    // docked Log panel wires this to refresh itself instead of polling.
    std::function<void()> onNewEntry;

private:
    EngineLog() = default;

    mutable std::mutex mutex_;
    std::deque<LogEntry> entries_;
    std::size_t totalEverWritten_ = 0; // monotonic count, even past kMaxEntries eviction -- what `cursor` indexes against.
    static constexpr std::size_t kMaxEntries = 5000;
};

} // namespace ce::diagnostics
