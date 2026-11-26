#include "engine/persistence.h"

namespace ce::persistence
{
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
