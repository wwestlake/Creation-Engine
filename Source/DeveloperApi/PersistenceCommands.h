#pragma once

#include "DeveloperApi.h"
#include <map>

namespace ce::developer
{
enum class PersistenceAction { info, open, inspect, release, lookup, query, changes };

// Commands execute serially on the persistence worker, separate from the
// editor or dedicated server's simulation thread.
class PersistenceCommands {
public:
    PersistenceCommands(PersistenceMount mount, Limits limits);
    bool Available() const noexcept;
    Value Execute(PersistenceAction action, Access access, const Value& parameters);
    void Expire();

private:
    using Clock = std::chrono::steady_clock;
    struct Snapshot {
        Access owner;
        Clock::time_point expires;
        std::unique_ptr<persistence::ReadSnapshot> read;
    };
    struct Continuation {
        Access owner;
        std::string snapshot;
        std::string query;
        Clock::time_point expires;
        persistence::QueryCursor cursor;
    };
    Snapshot& Find(const std::string& token, Access access);
    Value Query(Access access, const Value& parameters, Snapshot& snapshot, const std::string& id);

    PersistenceMount mount_;
    Limits limits_;
    std::map<std::string, Snapshot> snapshots_;
    std::map<std::string, Continuation> continuations_;
};
} // namespace ce::developer
