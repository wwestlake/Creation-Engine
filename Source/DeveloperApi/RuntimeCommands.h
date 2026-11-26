#pragma once

#include "Json.h"
#include <functional>

namespace ce::developer
{
inline constexpr int maxStepTicks = 120;

enum class Phase { stopped, running, paused };
enum class Source { authored, fixture };
enum class RuntimeAction { inspect, start, pause, step, resume, stop };

struct RuntimeOperations {
    std::function<Phase()> phase;
    std::function<std::uint64_t()> tick;
    std::function<bool(Source)> start;
    std::function<void()> pause;
    std::function<bool()> resume;
    std::function<bool()> stop;
    // Runs the normal simulation frame while leaving the host paused.
    std::function<void()> frame;
    std::function<Value()> diagnostics;
};

// Owned by the host's Engine thread. The editor transport buttons and the
// developer dispatcher use these same operations.
class RuntimeCommands {
public:
    explicit RuntimeCommands(RuntimeOperations operations) : operations_(std::move(operations)) {}
    Value Execute(RuntimeAction action, Source source = Source::authored, int ticks = 0);

private:
    Value Inspect() const;
    RuntimeOperations operations_;
    String session_;
    String source_;
    String lastError_;
};
} // namespace ce::developer
