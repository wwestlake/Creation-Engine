#pragma once

#include "DeveloperApi.h"
#include "engine/persistence_query.h"
#include <map>

namespace ce::developer
{
enum class PersistenceAction { info, open, inspect, release, lookup, query, changes, indexes, explain, execute };

// Commands execute serially on the persistence worker, separate from the
// editor or dedicated server's simulation thread.
class PersistenceCommands {
public:
    PersistenceCommands(PersistenceMount mount, Limits limits);
    bool Available() const noexcept;
    bool Supports(PersistenceAction action) const noexcept;
    Value Execute(PersistenceAction action, Access access, const Value& parameters,
                  std::chrono::steady_clock::time_point deadline, std::stop_token stop);
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
        std::variant<persistence::QueryCursor, persistence::PortableCursor> cursor;
    };
    Snapshot& Find(const std::string& token, Access access);
    const Continuation* Resume(const Value& parameters, Access access, const std::string& snapshot, const std::string& fingerprint) const;
    Value Publish(Continuation continuation);
    Value Query(Access access, const Value& parameters, Snapshot& snapshot, const std::string& id);
    Value PlannedQuery(PersistenceAction action, Access access, const Value& parameters, Snapshot& snapshot,
                       const std::string& id, Clock::time_point deadline, std::stop_token stop);

    PersistenceMount mount_;
    Limits limits_;
    persistence::CapabilitySet capabilities_;
    std::map<std::string, Snapshot> snapshots_;
    std::map<std::string, Continuation> continuations_;
};
} // namespace ce::developer
