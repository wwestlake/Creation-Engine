#pragma once

#include "RuntimeCommands.h"
#include "engine/persistence.h"

#include <chrono>
#include <memory>

namespace ce::developer
{
enum class Access : unsigned char { read, control };

struct Limits {
    int requestBytes = 65536;
    int responseBytes = 262144;
    int pageEntries = 32;
    int outstanding = 32;
    int queueDepth = 16;
    int concurrentJobs = 2;
    int snapshotCount = 16;
    int cursorCount = 256;
    int snapshotMs = 30000;
    int cursorMs = 15000;
    int deadlineMs = 5000;
    int resultMs = 60000;
    int retainedBytes = 16777216;
    int requestHistory = 8192;
};

struct Settings {
    bool enabled = false;
    bool allowFixture = false;
    bool allowAuthored = true;
    int port = 0;
    String readToken;
    String controlToken;
    String provider;
    String store;
    Limits limits;
};

Settings ReadSettings();

struct PersistenceMount {
    String provider;
    persistence::SessionId id;
    std::unique_ptr<persistence::StorageProvider> owner;
    std::unique_ptr<persistence::Session> session;
};

PersistenceMount MakeTestMount(const Settings& settings);

struct Response {
    int status;
    Value body;
};

class DeveloperApi {
public:
    DeveloperApi(Settings settings, String processMode, String engineVersion,
                 RuntimeCommands& runtime, PersistenceMount persistence = {});
    ~DeveloperApi();

    int Start();
    void Stop();
    void PumpOwner();
    Value Discovery(Access access) const;
    Value OpenApi() const;

    // Direct agent/UI bridges can enqueue exactly the same named commands.
    Response Submit(Access access, const std::string& command, const Value& body);
    Response Job(Access access, const std::string& id, bool cancel);
    const Limits& GetLimits() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace ce::developer
