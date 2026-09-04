#pragma once

#include <JuceHeader.h>

namespace creation::assets { class ProjectSession; }

namespace ce::diagnostics {

// Durable half of EngineLog: periodically drains new entries (via
// EngineLog::ConsumeSince) and appends them, as plain formatted text
// lines, to the active project's VFS at a fixed logical path -- NOT
// registered as a browsable AssetKind (a running log isn't a "thing" the
// Content Browser should list), just a raw entry read back on demand
// (e.g. "open the log file" tooling, not built yet -- this is the
// writing half only, per the instruction not to build more than asked).
//
// Best-effort: a failed flush is silently retried next tick with a
// growing backlog rather than raising an error -- this is diagnostic
// data, not something worth interrupting the user over.
class EngineLogVfsWriter final : private juce::Timer {
public:
    ~EngineLogVfsWriter() override;

    // Pass the newly-opened project's session to start periodic flushing
    // against it (flushing whatever was pending against the PREVIOUS
    // session first, if any); pass nullptr when a project closes to stop.
    void SetSession(creation::assets::ProjectSession* session);

private:
    void timerCallback() override;
    void FlushNow();

    creation::assets::ProjectSession* session_ = nullptr;
    std::size_t cursor_ = 0;

    static constexpr int kFlushIntervalMs = 5000;
    static constexpr std::int64_t kMaxLogFileBytes = 2 * 1024 * 1024; // soft cap; oldest lines trimmed past this.
};

} // namespace ce::diagnostics
