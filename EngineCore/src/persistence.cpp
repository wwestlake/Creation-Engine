#include "engine/persistence.h"

#include "persistence_validation.h"

#include <mutex>

namespace ce::persistence
{
namespace
{
using Clock = std::chrono::steady_clock;

struct SnapshotPin {
    std::mutex mutex;
    Clock::time_point expires;
    std::unique_ptr<ReadSnapshot> snapshot;
};

class ManagedSnapshot final : public ReadSnapshot {
public:
    ManagedSnapshot(std::shared_ptr<SnapshotPin> pin, std::size_t pageEntries)
        : pin_(std::move(pin)), revision_(pin_->snapshot->Revision()), pageEntries_(pageEntries) {}

    CommitRevision Revision() const noexcept override { return revision_; }

    Result<std::optional<Record>> Get(const RecordKey& key) const override
    {
        return Read([&](const auto& snapshot) { return snapshot.Get(key); });
    }

    Result<std::vector<std::optional<Record>>> GetMany(std::span<const RecordKey> keys) const override
    {
        return Read([&](const auto& snapshot) -> Result<std::vector<std::optional<Record>>> {
            if (keys.size() > pageEntries_)
                return detail::Fail(ErrorCode::ResourceLimit, "Bulk read exceeds the configured page limit.");
            return snapshot.GetMany(keys);
        });
    }

    Result<QueryPage> Query(const QueryDescription& query) const override
    {
        return Read([&](const auto& snapshot) -> Result<QueryPage> {
            if (query.limit > pageEntries_)
                return detail::Fail(ErrorCode::ResourceLimit, "Query exceeds the configured page limit.");
            auto expiry = pin_->expires;
            if (query.after && query.after->expiresAt) {
                if (Clock::now() >= *query.after->expiresAt)
                    return detail::Fail(ErrorCode::HistoryUnavailable, "Query cursor expired.");
                expiry = std::min(expiry, *query.after->expiresAt);
            }
            auto page = snapshot.Query(query);
            if (page && page.Value().next)
                page.Value().next->expiresAt = expiry;
            return page;
        });
    }

private:
    template <typename Function>
    auto Read(Function&& function) const -> decltype(function(std::declval<const ReadSnapshot&>()))
    {
        std::lock_guard lock(pin_->mutex);
        if (Clock::now() >= pin_->expires)
            pin_->snapshot.reset();
        if (!pin_->snapshot)
            return detail::Fail(ErrorCode::HistoryUnavailable, "Read snapshot expired.");
        return function(*pin_->snapshot);
    }

    std::shared_ptr<SnapshotPin> pin_;
    CommitRevision revision_;
    std::size_t pageEntries_;
};
} // namespace

struct Session::SnapshotRegistry {
    mutable std::mutex mutex;
    std::vector<std::weak_ptr<SnapshotPin>> pins;

    void Reap()
    {
        std::erase_if(pins, [](const auto& weak) {
            const auto pin = weak.lock();
            if (!pin)
                return true;
            if (Clock::now() < pin->expires)
                return false;
            std::unique_lock lock(pin->mutex, std::try_to_lock);
            if (!lock.owns_lock())
                return false; // A read begun before expiry can finish.
            pin->snapshot.reset();
            return true;
        });
    }
};

Session::Session(PersistencePolicy policy) : policy_(policy), snapshots_(std::make_unique<SnapshotRegistry>()) {}
Session::~Session() = default;

Result<std::unique_ptr<ReadSnapshot>> Session::OpenSnapshot() const
{
    std::lock_guard lock(snapshots_->mutex);
    snapshots_->Reap();
    if (snapshots_->pins.size() >= policy_.openSnapshots)
        return detail::Fail(ErrorCode::ResourceLimit, "Maximum open snapshots reached.");
    auto snapshot = CaptureSnapshot();
    if (!snapshot)
        return snapshot.Error();
    auto pin = std::make_shared<SnapshotPin>();
    pin->snapshot = std::move(snapshot).Value();
    pin->expires = Clock::now() + policy_.snapshotLifetime;
    snapshots_->pins.push_back(pin);
    return std::unique_ptr<ReadSnapshot>(std::make_unique<ManagedSnapshot>(std::move(pin), policy_.pageEntries));
}

Result<CommitRevision> Session::Commit(const Transaction& transaction)
{
    if (transaction.mutations.size() > policy_.recordsPerTransaction)
        return detail::Fail(ErrorCode::ResourceLimit, "Transaction exceeds the configured record limit.");
    {
        std::lock_guard lock(snapshots_->mutex);
        snapshots_->Reap();
    }
    return Apply(transaction);
}

Result<ChangePage> Session::ReadChanges(CommitRevision after, std::size_t limit) const
{
    if (limit > policy_.pageEntries)
        return detail::Fail(ErrorCode::ResourceLimit, "Journal read exceeds the configured page limit.");
    {
        std::lock_guard lock(snapshots_->mutex);
        snapshots_->Reap();
    }
    return CaptureChanges(after, limit);
}
Result<std::vector<std::optional<Record>>> ReadSnapshot::GetMany(std::span<const RecordKey> keys) const
{
    std::vector<std::optional<Record>> records;
    records.reserve(keys.size());
    for (const auto& key : keys) {
        auto result = Get(key);
        if (!result)
            return result.Error();
        records.push_back(std::move(result).Value());
    }
    return records;
}

Result<std::optional<Record>> Session::Get(const RecordKey& key) const
{
    auto snapshot = OpenSnapshot();
    if (!snapshot)
        return snapshot.Error();
    return snapshot.Value()->Get(key);
}

Result<std::vector<std::optional<Record>>> Session::GetMany(std::span<const RecordKey> keys) const
{
    auto snapshot = OpenSnapshot();
    if (!snapshot)
        return snapshot.Error();
    return snapshot.Value()->GetMany(keys);
}

Result<QueryPage> Session::Query(const QueryDescription& query) const
{
    auto snapshot = OpenSnapshot();
    if (!snapshot)
        return snapshot.Error();
    return snapshot.Value()->Query(query);
}
} // namespace ce::persistence
