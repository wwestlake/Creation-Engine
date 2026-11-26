#include "RuntimeCommands.h"

namespace ce::developer
{
Value RuntimeCommands::Inspect() const
{
    const auto state = operations_.phase();
    const char* phase = state == Phase::running ? "running" : state == Phase::paused ? "paused" : "stopped";
    return Object({{"session", session_}, {"state", phase}, {"source", source_},
                   {"tick", Decimal(operations_.tick())}, {"lastError", lastError_},
                   {"diagnostics", operations_.diagnostics ? operations_.diagnostics() : Object()}});
}

Value RuntimeCommands::Execute(RuntimeAction action, Source source, int ticks)
{
    if (action == RuntimeAction::inspect)
        return Inspect();
    const auto phase = operations_.phase();
    const auto require = [&](Phase expected) {
        if (phase != expected)
            Reject("invalid_transition", "The command is unavailable in the current runtime state.", 409);
    };
    const auto failed = [&](const char* message) {
        lastError_ = message;
        Reject("operation_failed", message, 409);
    };
    switch (action) {
        case RuntimeAction::start:
            require(Phase::stopped);
            if (!operations_.start(source))
                failed("The Engine could not start the selected runtime source.");
            session_ = juce::Uuid().toString();
            source_ = source == Source::fixture ? "fixture" : "authored";
            break;
        case RuntimeAction::pause:
            require(Phase::running);
            operations_.pause();
            break;
        case RuntimeAction::resume:
            require(Phase::paused);
            if (!operations_.resume())
                failed("The Engine could not resume the runtime.");
            break;
        case RuntimeAction::step: {
            require(Phase::paused);
            if (ticks < 1 || ticks > maxStepTicks)
                Reject("invalid_input", "Ticks must be between 1 and 120.");
            const auto before = operations_.tick();
            for (int i = 0; i < ticks; ++i)
                operations_.frame();
            if (operations_.tick() - before != static_cast<std::uint64_t>(ticks))
                failed("The Engine did not advance the requested tick count.");
            lastError_.clear();
            auto result = Inspect();
            Put(result, "advanced", ticks);
            return result;
        }
        case RuntimeAction::stop:
            if (phase == Phase::stopped)
                Reject("invalid_transition", "There is no active runtime to stop.", 409);
            if (!operations_.stop())
                failed("The Engine could not restore its stopped state.");
            break;
        case RuntimeAction::inspect:
            break;
    }
    lastError_.clear();
    return Inspect();
}
} // namespace ce::developer
