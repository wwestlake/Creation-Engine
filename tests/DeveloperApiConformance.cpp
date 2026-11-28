#include "DeveloperApi/DeveloperApi.h"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <semaphore>
#include <stdexcept>
#include <thread>

namespace {
using namespace ce::developer;
using namespace std::chrono_literals;
using namespace ce::persistence;

void Require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

std::string Id() { return juce::Uuid().toString().toStdString(); }

Value SubmitBody(const std::string& id, Value args)
{
    return Object({{"requestId", Utf8(id)}, {"arguments", std::move(args)}});
}

Value Terminal(DeveloperApi& api, const std::string& id)
{
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (std::chrono::steady_clock::now() < deadline) {
        const auto job = api.Job(Access::read, id, false);
        Require(job.status == 200, "read job response");
        const auto state = Get(job.body, "state").toString();
        if (state != "queued" && state != "running") return job.body;
        std::this_thread::sleep_for(1ms);
    }
    throw std::runtime_error("job did not complete");
}

Value Execute(DeveloperApi& api, const char* command, Value args = Object())
{
    const auto id = Id();
    Require(api.Submit(Access::read, command, SubmitBody(id, std::move(args))).status == 202, "submit persistence command");
    const auto job = Terminal(api, id);
    Require(Get(job, "state").toString() == "succeeded", "complete persistence command");
    return Get(job, "result");
}

Settings TestSettings()
{
    Settings settings;
    settings.enabled = true;
    settings.allowFixture = true;
    settings.readToken = juce::Uuid().toString();
    settings.controlToken = juce::Uuid().toString();
    return settings;
}

void SnapshotIsolation(const char* provider, const std::filesystem::path& path)
{
    auto settings = TestSettings();
    settings.provider = provider;
    settings.store = String(path.string());
    auto mount = MakeTestMount(settings);
    auto* nativeSession = mount.session.get();
    RuntimeCommands runtime({});
    DeveloperApi api(settings, "server", "test", runtime, std::move(mount));
    api.Start();

    const auto first = Execute(api, "persistence.snapshot.open");
    const CollectionKey records{{"automation"}, {"records"}};
    const RecordData payload{{1}, {{{"value"}, std::int64_t{42}}}, Binary{0, 128, 255}};
    const auto commit = nativeSession->Commit({{{MutationKind::Create, {records, {"new"}}, payload, {}}}});
    Require(commit && commit.Value().value == 5, "native writer advances durable revision");

    const auto second = Execute(api, "persistence.snapshot.open");
    const auto query = [&](const Value& snap) {
        return Execute(api, "persistence.query", Object({{"snapshot", Get(snap, "snapshot")}, {"namespace", "YXV0b21hdGlvbg=="},
                       {"collection", "cmVjb3Jkcw=="}, {"index", "dmFsdWU="},
                       {"prefix", Array({Object({{"type", "i64"}, {"value", "42"}})})}, {"limit", 1}}));
    };
    const auto oldView = query(first);
    const auto newView = query(second);
    Require(Get(oldView, "revision").toString() == "4" && Get(oldView, "records").size() == 0, "old API snapshot pins old view");
    Require(Get(newView, "revision").toString() == "5" && Get(newView, "records").size() == 1, "new API snapshot sees native commit");

    const auto catalog = [&](const Value& snap) {
        return Execute(api, "persistence.indexes", Object({{"snapshot", Get(snap, "snapshot")}, {"namespace", "YXV0b21hdGlvbg=="},
                       {"collection", "cmVjb3Jkcw=="}}));
    };
    const auto added = nativeSession->MaintainIndex({records, {"alternate"}, IndexOperation::Create,
        IndexDefinition{{FieldId{"measurement"}}, false, {2}, {SortDirection::Descending}}});
    Require(added && added.Value().revision.value == 5, "native index publication preserves record revision");
    const auto third = Execute(api, "persistence.snapshot.open");
    Require(Get(catalog(second), "indexes").size() == 1 &&
            Get(catalog(third), "indexes").size() == 2, "API index catalog pins the exact snapshot");
}

struct QueryGate {
    std::binary_semaphore entered{0}, resume{0};
};

class GatedSnapshot final : public ReadSnapshot {
public:
    GatedSnapshot(std::unique_ptr<ReadSnapshot> inner, QueryGate& gate)
        : inner_(std::move(inner)), gate_(gate) {}
    CommitRevision Revision() const noexcept override { return inner_->Revision(); }
    Result<std::optional<Record>> Get(const RecordKey& key) const override { return inner_->Get(key); }
    Result<CollectionSchema> Describe(const CollectionKey& key) const override { return inner_->Describe(key); }
    Result<QueryPage> Query(const QueryDescription& query) const override
    {
        gate_.entered.release();
        gate_.resume.acquire();
        return inner_->Query(query);
    }

private:
    std::unique_ptr<ReadSnapshot> inner_;
    QueryGate& gate_;
};

class GatedSession final : public Session {
public:
    GatedSession(std::unique_ptr<Session> inner, QueryGate& gate)
        : Session({}), inner_(std::move(inner)), gate_(gate) {}
    Status Close() override { return inner_->Close(); }
    Result<SessionArchive> Export() const override { return inner_->Export(); }

private:
    Result<CommitRevision> Apply(const Transaction& change) override { return inner_->Commit(change); }
    Result<ChangePage> CaptureChanges(CommitRevision after, std::size_t limit) const override
    {
        return inner_->ReadChanges(after, limit);
    }
    Result<std::unique_ptr<ReadSnapshot>> CaptureSnapshot() const override
    {
        auto snapshot = inner_->OpenSnapshot();
        if (!snapshot) return snapshot.Error();
        return std::unique_ptr<ReadSnapshot>(std::make_unique<GatedSnapshot>(std::move(snapshot).Value(), gate_));
    }
    std::unique_ptr<Session> inner_;
    QueryGate& gate_;
};

void QueryCancellation()
{
    auto settings = TestSettings();
    auto mount = MakeTestMount(settings);
    QueryGate gate;
    mount.session = std::make_unique<GatedSession>(std::move(mount.session), gate);
    RuntimeCommands runtime({});
    DeveloperApi api(settings, "server", "test", runtime, std::move(mount));
    api.Start();
    const auto snap = Execute(api, "persistence.snapshot.open");
    const auto args = Object({{"snapshot", Get(snap, "snapshot")}, {"namespace", "YXV0b21hdGlvbg=="},
        {"collection", "cmVjb3Jkcw=="}, {"allowScan", true}, {"limit", 1}});
    const auto id = Id();
    Require(api.Submit(Access::read, "persistence.query.execute", SubmitBody(id, args)).status == 202,
            "submit cancellable portable query");
    const bool entered = gate.entered.try_acquire_for(3s);
    const auto running = api.Job(Access::read, id, false);
    const auto cancel = api.Job(Access::read, id, true);
    gate.resume.release();
    Require(entered && running.status == 200 && Get(running.body, "state").toString() == "running" &&
            static_cast<bool>(Get(running.body, "cancellable")), "running query is cancellable at a provider boundary");
    Require(cancel.status == 200, "running query accepted cancellation");
    const auto settled = Terminal(api, id);
    Require(Get(settled, "state").toString() == "cancelled" && Get(settled, "result").isVoid(),
            "cancelled query did not publish a page");
}

void ExecutionBoundaries()
{
    auto settings = TestSettings();
    settings.limits.deadlineMs = 100;
    settings.limits.resultMs = 250;
    settings.limits.requestHistory = 2;
    std::atomic_bool running = false, release = false;
    RuntimeCommands runtime({
        [&] {
            running.store(true);
            running.notify_one();
            release.wait(false);
            return Phase::stopped;
        },
        [] { return std::uint64_t{0}; }, {}, {}, {}, {}, {}, {}
    });
    DeveloperApi api(settings, "server", "test", runtime);
    api.Start();

    const auto id = Id();
    const auto held = SubmitBody(id, Object());
    Require(api.Submit(Access::read, "runtime.inspect", held).status == 202, "queue job");
    std::jthread checker([&] {
        running.wait(false);
        const auto active = api.Job(Access::read, id, false);
        const auto cancelled = api.Job(Access::read, id, true);
        const auto queued = Id();
        const auto deadlineJob = SubmitBody(queued, Object());
        const auto accepted = api.Submit(Access::read, "runtime.inspect", deadlineJob);
        std::this_thread::sleep_for(130ms);
        const auto expired = api.Job(Access::read, queued, false);
        const bool pass = active.status == 200 && Get(active.body, "state").toString() == "running" &&
            !static_cast<bool>(Get(active.body, "cancellable")) && cancelled.status == 409 &&
            accepted.status == 202 && expired.status == 200 && Get(expired.body, "state").toString() == "timed_out";
        release.store(true);
        release.notify_one();
        if (!pass) std::cerr << "Running/cancellation/deadline verification failed.\n";
        running.store(pass);
    });
    api.PumpOwner();
    checker.join();
    Require(running.load(), "running job cannot be cancelled and queued job honors deadline");
    const auto finished = Terminal(api, id);
    Require(Get(finished, "state").toString() == "succeeded" &&
            static_cast<bool>(Get(finished, "deadlineExceeded")), "running job reports actual outcome after deadline");
    std::this_thread::sleep_for(270ms);
    Require(api.Job(Access::read, id, false).status == 410, "result expires");
    Require(api.Submit(Access::read, "runtime.inspect", held).status == 410, "expired response does not repeat request");
    Require(api.Submit(Access::read, "runtime.inspect", SubmitBody(Id(), Object())).status == 429, "history capacity is enforced");
}
} // namespace

int main(int argc, char** argv)
{
    if (argc != 2) return 2;
    try {
        const auto path = std::filesystem::absolute(argv[1]) / Id();
        std::filesystem::create_directories(path);
        struct Scratch {
            std::filesystem::path path;
            ~Scratch() { std::error_code error; std::filesystem::remove_all(path, error); }
        } scratch{path};
        for (const auto* provider : {"memory", "sqlite", "rocksdb"})
            SnapshotIsolation(provider, path / provider);
        QueryCancellation();
        ExecutionBoundaries();
        std::cout << "Developer API native write isolation and execution boundaries passed.\n";
        return 0;
    } catch (const Failure& error) {
        std::cerr << error.message << '\n';
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
    }
    return 1;
}
