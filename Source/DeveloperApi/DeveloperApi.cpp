#include "DeveloperApi.h"
#include "HttpTransport.h"
#include "PersistenceCommands.h"

#include <algorithm>
#include <array>
#include <juce_cryptography/juce_cryptography.h>
#include <condition_variable>
#include <deque>
#include <regex>
#include <map>
#include <mutex>
#include <thread>
#include <variant>

namespace ce::developer
{
namespace
{
using Clock = std::chrono::steady_clock;
using Action = std::variant<RuntimeAction, PersistenceAction>;

struct Command {
    const char* name;
    Access permission;
    Action action;
};

constexpr Command commands[] = {
    {"runtime.inspect", Access::read, RuntimeAction::inspect},
    {"runtime.start", Access::control, RuntimeAction::start},
    {"runtime.pause", Access::control, RuntimeAction::pause},
    {"runtime.step", Access::control, RuntimeAction::step},
    {"runtime.resume", Access::control, RuntimeAction::resume},
    {"runtime.stop", Access::control, RuntimeAction::stop},
    {"persistence.info", Access::read, PersistenceAction::info},
    {"persistence.snapshot.open", Access::read, PersistenceAction::open},
    {"persistence.snapshot.inspect", Access::read, PersistenceAction::inspect},
    {"persistence.snapshot.release", Access::read, PersistenceAction::release},
    {"persistence.record.get", Access::read, PersistenceAction::lookup},
    {"persistence.query", Access::read, PersistenceAction::query},
    {"persistence.changes", Access::read, PersistenceAction::changes},
};

Value Type(const char* type) { return Object({{"type", type}}); }
Value Integer(int low, int high) { return Object({{"type", "integer"}, {"minimum", low}, {"maximum", high}}); }
Value List(Value items) { return Object({{"type", "array"}, {"items", std::move(items)}}); }
Value Choice(std::initializer_list<Value> values) { return Object({{"enum", Array(values)}}); }
Value Nullable(Value value) { return Object({{"anyOf", Array({std::move(value), Type("null")})}}); }
Value DecimalSchema() { return Object({{"type", "string"}, {"pattern", "^(0|[1-9][0-9]{0,19})$"}}); }
Value Bytes() { return Object({{"type", "string"}, {"contentEncoding", "base64"}}); }
Value Identifier() { auto value = Bytes(); Put(value, "minLength", 4); Put(value, "maxLength", 1368); return value; }
Value Token() { return Object({{"type", "string"}, {"pattern", "^[a-fA-F0-9-]{32,36}$"}}); }
Value RequestId() { return Object({{"type", "string"}, {"pattern", "^[a-zA-Z0-9_-]{16,64}$"}}); }

Value Struct(std::initializer_list<std::pair<const char*, Value>> fields, std::initializer_list<Value> required)
{
    return Object({{"type", "object"}, {"properties", Object(fields)}, {"required", Array(required)},
                   {"additionalProperties", false}});
}

Value FieldSchema()
{
    return Object({{"oneOf", Array({
        Struct({{"type", Choice({"null"})}}, {"type"}),
        Struct({{"type", Choice({"bool"})}, {"value", Type("boolean")}}, {"type", "value"}),
        Struct({{"type", Choice({"i64"})}, {"value", Object({{"type", "string"}, {"pattern", "^-?(0|[1-9][0-9]{0,18})$"}})}}, {"type", "value"}),
        Struct({{"type", Choice({"real"})}, {"value", Object({{"type", "string"}, {"maxLength", 32},
            {"pattern", "^-?[0-9]+(\\.[0-9]+)?([eE][+-]?[0-9]+)?$"}})}}, {"type", "value"}),
        Struct({{"type", Choice({"text", "bytes"})}, {"value", Bytes()}}, {"type", "value"})
    })}});
}

Value RecordSchema()
{
    return Struct({{"id", Identifier()}, {"version", DecimalSchema()}, {"schemaVersion", DecimalSchema()},
                   {"fields", List(Struct({{"name", Identifier()}, {"field", FieldSchema()}}, {"name", "field"}))},
                   {"binary", Nullable(Bytes())}}, {"id", "version", "schemaVersion", "fields", "binary"});
}

Value Parameters(const Command& command, const Settings& settings)
{
    if (command.action == Action{RuntimeAction::start}) {
        const auto source = settings.allowFixture && settings.allowAuthored ? Choice({"authored", "fixture"})
                           : settings.allowFixture ? Choice({"fixture"}) : Choice({"authored"});
        return Struct({{"source", source}}, {"source"});
    }
    if (command.action == Action{RuntimeAction::step})
        return Struct({{"ticks", Integer(1, maxStepTicks)}}, {"ticks"});
    if (const auto* action = std::get_if<PersistenceAction>(&command.action)) {
        const auto limit = Integer(1, settings.limits.pageEntries);
        switch (*action) {
            case PersistenceAction::inspect:
            case PersistenceAction::release:
                return Struct({{"snapshot", Token()}}, {"snapshot"});
            case PersistenceAction::lookup:
                return Struct({{"snapshot", Token()}, {"namespace", Identifier()}, {"collection", Identifier()},
                               {"id", Identifier()}}, {"snapshot", "namespace", "collection", "id"});
            case PersistenceAction::query: {
                const auto bound = Struct({{"value", FieldSchema()}, {"inclusive", Type("boolean")}}, {"value", "inclusive"});
                return Struct({{"snapshot", Token()}, {"namespace", Identifier()}, {"collection", Identifier()},
                               {"index", Identifier()}, {"prefix", List(FieldSchema())}, {"lower", bound}, {"upper", bound},
                               {"limit", limit}, {"cursor", Token()}}, {"snapshot", "namespace", "collection", "limit"});
            }
            case PersistenceAction::changes:
                return Struct({{"after", DecimalSchema()}, {"limit", limit}}, {"after", "limit"});
            default: break;
        }
    }
    return Struct({}, {});
}

Value ResultSchema(const Command& command)
{
    if (std::holds_alternative<RuntimeAction>(command.action)) {
        auto result = Struct({{"session", Type("string")}, {"state", Choice({"stopped", "running", "paused"})},
                              {"tick", DecimalSchema()}, {"source", Choice({"", "authored", "fixture"})},
                              {"executionMode", Choice({"editor", "server"})}, {"lastError", Type("string")},
                              {"diagnostics", Type("object")}},
                             {"session", "state", "tick", "source", "executionMode", "lastError", "diagnostics"});
        if (command.action == Action{RuntimeAction::step}) {
            auto properties = Get(result, "properties");
            Put(properties, "advanced", Integer(1, maxStepTicks));
            auto required = Get(result, "required");
            required.append("advanced");
            Put(result, "required", required);
        }
        return result;
    }
    switch (std::get<PersistenceAction>(command.action)) {
        case PersistenceAction::info:
            return Struct({{"provider", Type("string")}, {"session", Identifier()}, {"revision", DecimalSchema()},
                           {"historyFloor", DecimalSchema()}, {"capabilities", List(Type("string"))}},
                          {"provider", "session", "revision", "historyFloor", "capabilities"});
        case PersistenceAction::open:
        case PersistenceAction::inspect:
            return Struct({{"snapshot", Token()}, {"revision", DecimalSchema()}, {"valid", Type("boolean")}},
                          {"snapshot", "revision", "valid"});
        case PersistenceAction::release:
            return Struct({{"released", Type("boolean")}}, {"released"});
        case PersistenceAction::lookup:
            return Struct({{"record", Nullable(RecordSchema())}, {"revision", DecimalSchema()}}, {"record", "revision"});
        case PersistenceAction::query:
            return Struct({{"records", List(RecordSchema())}, {"revision", DecimalSchema()}, {"cursor", Nullable(Token())}},
                          {"records", "revision", "cursor"});
        case PersistenceAction::changes: {
            const auto change = Struct({{"kind", Choice({"create", "replace", "erase"})}, {"namespace", Identifier()},
                                        {"collection", Identifier()}, {"id", Identifier()}, {"record", Nullable(RecordSchema())},
                                        {"previousVersion", Nullable(DecimalSchema())}},
                                       {"kind", "namespace", "collection", "id", "record", "previousVersion"});
            const auto batch = Struct({{"revision", DecimalSchema()}, {"changes", List(change)}}, {"revision", "changes"});
            return Struct({{"batches", List(batch)}, {"revision", DecimalSchema()}, {"historyFloor", DecimalSchema()},
                           {"next", DecimalSchema()}, {"more", Type("boolean")}},
                          {"batches", "revision", "historyFloor", "next", "more"});
        }
    }
    return {};
}

// The supported schema vocabulary also validates requests. Native operations
// check canonical encodings, numeric overflow, and constraints requiring state.
bool Matches(const Value& value, const Value& schema)
{
    if (const auto alternatives = Get(schema, "oneOf"); alternatives.isArray())
        return std::ranges::count_if(*alternatives.getArray(), [&](const auto& item) { return Matches(value, item); }) == 1;
    if (const auto choices = Get(schema, "enum"); choices.isArray())
        return choices.getArray()->contains(value);
    const auto type = Get(schema, "type").toString();
    if (type == "string") {
        const auto min = static_cast<int>(Get(schema, "minLength"));
        const auto max = static_cast<int>(Get(schema, "maxLength"));
        if (!value.isString() || value.toString().length() < min || (max != 0 && value.toString().length() > max))
            return false;
        const auto pattern = Text(Get(schema, "pattern"));
        return pattern.empty() || std::regex_match(Text(value), std::regex(pattern));
    }
    if (type == "integer") {
        const auto number = static_cast<juce::int64>(value);
        return (value.isInt() || value.isInt64()) && number >= static_cast<int>(Get(schema, "minimum"))
                                               && number <= static_cast<int>(Get(schema, "maximum"));
    }
    if (type == "number") return value.isDouble() || value.isInt() || value.isInt64();
    if (type == "boolean") return value.isBool();
    if (type == "array")
        return value.isArray() && std::ranges::all_of(*value.getArray(), [&](const auto& item) { return Matches(item, Get(schema, "items")); });
    if (type == "object") {
        const auto* object = value.getDynamicObject();
        if (!object) return false;
        const auto properties = Get(schema, "properties");
        for (const auto& name : *Get(schema, "required").getArray())
            if (!object->hasProperty(juce::Identifier(name.toString())))
                return false;
        for (const auto& field : object->getProperties()) {
            const auto property = properties.getProperty(field.name, {});
            if (property.isVoid() || !Matches(field.value, property)) return false;
        }
        return true;
    }
    return false;
}

std::string Fingerprint(const Value& value)
{
    const auto json = juce::JSON::toString(Canonical(value), true);
    return juce::SHA256(json.toRawUTF8(), json.getNumBytesAsUTF8()).toHexString().toStdString();
}

constexpr auto limitFields = std::to_array<std::pair<const char*, int Limits::*>>({
    {"requestBytes", &Limits::requestBytes}, {"responseBytes", &Limits::responseBytes},
    {"pageEntries", &Limits::pageEntries}, {"outstanding", &Limits::outstanding},
    {"queueDepth", &Limits::queueDepth}, {"concurrentJobs", &Limits::concurrentJobs},
    {"snapshotCount", &Limits::snapshotCount}, {"cursorCount", &Limits::cursorCount},
    {"snapshotMs", &Limits::snapshotMs}, {"cursorMs", &Limits::cursorMs}, {"deadlineMs", &Limits::deadlineMs},
    {"resultMs", &Limits::resultMs}, {"retainedBytes", &Limits::retainedBytes}, {"requestHistory", &Limits::requestHistory}
});

Value LimitsJson(const Limits& limits)
{
    auto result = Object({{"maxStepTicks", maxStepTicks}});
    for (const auto& [name, member] : limitFields) Put(result, name, limits.*member);
    return result;
}

bool ValidId(std::string_view id)
{
    return id.size() >= 16 && id.size() <= 64 && std::ranges::all_of(id, [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
    });
}

Response FailureResponse(const Failure& failure)
{
    return {failure.httpStatus, Object({{"error", Error(failure)}})};
}
} // namespace

Settings ReadSettings()
{
    const auto env = [](const char* name) { return juce::SystemStats::getEnvironmentVariable(name, {}); };
    Settings settings;
    settings.enabled = env("DJEHUTI_DEV_ENABLE") == "1";
    if (!settings.enabled) return settings;
    settings.allowFixture = env("DJEHUTI_DEV_FIXTURE") == "1";
    settings.readToken = env("DJEHUTI_DEV_READ_TOKEN");
    settings.controlToken = env("DJEHUTI_DEV_CONTROL_TOKEN");
    if (!ValidId(settings.readToken.toStdString()) || !ValidId(settings.controlToken.toStdString()) ||
        settings.readToken.length() < 32 || settings.controlToken.length() < 32 || settings.readToken == settings.controlToken)
        Reject("invalid_input", "Configure distinct 32–64 character URL-safe read and control credentials.");
    const auto port = env("DJEHUTI_DEV_PORT");
    if (port.isNotEmpty()) {
        settings.port = ParseDecimal<int>(port);
        if (settings.port < 0 || settings.port > 65535) Reject("invalid_input", "Invalid Developer API port.");
    }
    settings.provider = env("DJEHUTI_DEV_PROVIDER");
    settings.store = env("DJEHUTI_DEV_STORE");
    const auto limits = env("DJEHUTI_DEV_LIMITS");
    if (limits.isNotEmpty()) {
        Value parsed;
        if (juce::JSON::parse(limits, parsed).failed() || !parsed.isObject())
            Reject("invalid_input", "Developer API limits must be a JSON object.");
        for (const auto& field : parsed.getDynamicObject()->getProperties()) {
            const auto setting = std::ranges::find_if(limitFields, [&](const auto& item) { return field.name.toString() == item.first; });
            if (setting == limitFields.end() || !field.value.isInt() || static_cast<int>(field.value) < 1)
                Reject("invalid_input", "Unknown or invalid Developer API limit.");
            settings.limits.*(setting->second) = static_cast<int>(field.value);
        }
        const auto& l = settings.limits;
        if (l.requestBytes > 1048576 || l.responseBytes < 65536 || l.responseBytes > 1048576 || l.pageEntries > 256 ||
            l.outstanding > 256 || l.queueDepth > 256 || l.concurrentJobs > 2 || l.snapshotCount > 64 ||
            l.cursorCount > 1024 || l.snapshotMs > 600000 || l.cursorMs > 600000 || l.deadlineMs > 60000 ||
            l.resultMs > 600000 || l.retainedBytes < l.responseBytes || l.retainedBytes > 67108864 ||
            l.requestHistory > 16384 || l.outstanding > l.requestHistory)
            Reject("invalid_input", "Developer API resource limits are out of range.");
    }
    return settings;
}

struct DeveloperApi::Impl {
    enum class State { queued, running, succeeded, failed, cancelled, timedOut };
    struct Job {
        std::string id;
        Access access;
        const Command* command;
        Value arguments;
        std::string fingerprint;
        Clock::time_point deadline;
        State state = State::queued;
        Clock::time_point expires{};
        std::size_t bytes = 0;
        bool deadlineExceeded = false;
        Value result;
        Value error;
    };

    Impl(Settings input, String mode, String version, RuntimeCommands& runtime, PersistenceMount mount)
        : settings(std::move(input)), mode(std::move(mode)), version(std::move(version)), runtime(runtime),
          persistence(std::move(mount), settings.limits) {}

    bool Available(const Command& command) const
    {
        if (command.action == Action{RuntimeAction::start})
            return settings.allowAuthored || settings.allowFixture;
        return std::holds_alternative<RuntimeAction>(command.action) || persistence.Available();
    }

    Value Describe(const Command& command, Access access) const
    {
        return Object({{"id", command.name}, {"version", 1}, {"available", Available(command)},
                       {"allowed", access == Access::control || command.permission == Access::read},
                       {"permission", command.permission == Access::read ? "read" : "control"},
                       {"parameters", Parameters(command, settings)}, {"result", ResultSchema(command)},
                       {"execution", std::holds_alternative<RuntimeAction>(command.action) ? "engine_owner" : "persistence_worker"}});
    }

    static bool Active(const Job& job) { return job.state == State::queued || job.state == State::running; }
    void EvictResults(Clock::time_point now)
    {
        while (!retained.empty() && (retained.front()->expires <= now ||
               retainedBytes > static_cast<std::size_t>(settings.limits.retainedBytes))) {
            auto& oldest = *retained.front();
            retained.pop_front();
            retainedBytes -= oldest.bytes;
            oldest.expires = now;
            oldest.result = Value{};
            oldest.error = Value{};
        }
    }

    void Finish(Job& job, State state, Value result, Value error, std::size_t bytes = 0)
    {
        const auto now = Clock::now();
        job.state = state;
        job.deadlineExceeded = now >= job.deadline;
        job.expires = now + std::chrono::milliseconds(settings.limits.resultMs);
        job.arguments = Value{};
        job.result = std::move(result);
        job.error = std::move(error);
        job.bytes = bytes + 1024; // Account for the envelope and small error payloads as well.
        retainedBytes += job.bytes;
        retained.push_back(&job);
        EvictResults(now);
    }

    void Sweep()
    {
        const auto now = Clock::now();
        for (auto& [key, job] : jobs) {
            if (job.state == State::queued && now >= job.deadline)
                Finish(job, State::timedOut, {}, Error({"timeout", "The operation reached its deadline before execution.", 408, {}}));
        }
        EvictResults(now);
    }

    Response Report(const Job& job, int status) const
    {
        if (!Active(job) && Clock::now() >= job.expires)
            return FailureResponse({"result_expired", "The job result has expired. The same request will not run again.", 410, {}});
        const char* states[] = {"queued", "running", "succeeded", "failed", "cancelled", "timed_out"};
        auto value = Object({{"id", Utf8(job.id)}, {"command", job.command->name},
                             {"state", states[static_cast<std::size_t>(job.state)]},
                             {"cancellable", job.state == State::queued},
                             {"deadlineExceeded", job.deadlineExceeded || (Active(job) && Clock::now() >= job.deadline)}});
        if (!job.result.isVoid()) Put(value, "result", job.result);
        if (!job.error.isVoid()) Put(value, "error", job.error);
        return {status, value};
    }

    Job* Take(bool owner)
    {
        Sweep();
        if (running >= settings.limits.concurrentJobs) return nullptr;
        Job* chosen = nullptr;
        for (auto& [key, job] : jobs) {
            if (job.state == State::queued && std::holds_alternative<RuntimeAction>(job.command->action) == owner &&
                (!chosen || job.deadline < chosen->deadline))
                chosen = &job;
        }
        if (chosen) {
            chosen->state = State::running;
            ++running;
        }
        return chosen;
    }

    void Execute(Job& job)
    {
        Value result;
        Value error;
        std::size_t bytes = 0;
        try {
            if (const auto* command = std::get_if<RuntimeAction>(&job.command->action)) {
                const auto source = Get(job.arguments, "source").toString() == "fixture" ? Source::fixture : Source::authored;
                result = runtime.Execute(*command, source, static_cast<int>(Get(job.arguments, "ticks")));
                Put(result, "executionMode", mode);
            } else {
                result = persistence.Execute(std::get<PersistenceAction>(job.command->action), job.access, job.arguments);
            }
            bytes = juce::JSON::toString(result, true).getNumBytesAsUTF8();
            if (bytes + 1024 > static_cast<std::size_t>(settings.limits.responseBytes))
                Reject("resource_limit", "The result exceeds the response size limit.", 429);
        } catch (const Failure& failure) {
            error = Error(failure);
        } catch (...) {
            error = Error({"internal", "The Engine command failed.", 500, {}});
        }
        if (!error.isVoid()) result = Value{};
        {
            std::lock_guard lock(mutex);
            const auto state = error.isVoid() ? State::succeeded : State::failed;
            Finish(job, state, std::move(result), std::move(error), state == State::succeeded ? bytes : 0);
            --running;
        }
        changed.notify_all();
    }

    void Work()
    {
        std::unique_lock lock(mutex);
        while (!stopping) {
            if (auto* job = Take(false)) {
                lock.unlock();
                Execute(*job);
                lock.lock();
            } else {
                changed.wait_for(lock, std::chrono::milliseconds(25));
                lock.unlock();
                persistence.Expire();
                lock.lock();
            }
        }
    }

    Settings settings;
    String mode, version;
    RuntimeCommands& runtime;
    PersistenceCommands persistence;
    std::unique_ptr<HttpTransport> http;
    std::thread worker;
    std::mutex mutex;
    std::condition_variable changed;
    std::map<std::pair<Access, std::string>, Job> jobs;
    std::deque<Job*> retained;
    std::size_t retainedBytes = 0;
    bool stopping = false;
    int running = 0;
};

DeveloperApi::DeveloperApi(Settings settings, String processMode, String engineVersion,
                           RuntimeCommands& runtime, PersistenceMount persistence)
    : impl_(std::make_unique<Impl>(std::move(settings), std::move(processMode), std::move(engineVersion),
                                    runtime, std::move(persistence))) {}

DeveloperApi::~DeveloperApi() { Stop(); }

int DeveloperApi::Start()
{
    if (!impl_->settings.enabled) return 0;
    if (impl_->http) Reject("conflict", "The Developer API is already listening.", 409);
    {
        std::lock_guard lock(impl_->mutex);
        impl_->stopping = false;
    }
    impl_->http = std::make_unique<HttpTransport>(*this, impl_->settings);
    const auto port = impl_->http->Start(impl_->settings.port);
    impl_->worker = std::thread([this] { impl_->Work(); });
    return port;
}

void DeveloperApi::Stop()
{
    if (impl_->http) {
        impl_->http->Stop();
        impl_->http.reset();
    }
    {
        std::lock_guard lock(impl_->mutex);
        impl_->stopping = true;
    }
    impl_->changed.notify_all();
    if (impl_->worker.joinable()) impl_->worker.join();
}

void DeveloperApi::PumpOwner()
{
    Impl::Job* job = nullptr;
    {
        std::lock_guard lock(impl_->mutex);
        if (!impl_->stopping) job = impl_->Take(true);
    }
    if (job) impl_->Execute(*job);
}

const Limits& DeveloperApi::GetLimits() const noexcept { return impl_->settings.limits; }

Value DeveloperApi::Discovery(Access access) const
{
    juce::Array<Value> catalog;
    for (const auto& command : commands) catalog.add(impl_->Describe(command, access));
    auto capabilities = Array({"runtime", "jobs"});
    if (impl_->settings.allowAuthored) capabilities.append("authored_runtime");
    if (impl_->settings.allowFixture) capabilities.append("synthetic_runtime");
    if (impl_->persistence.Available()) capabilities.append("persistence_inspection");
    return Object({{"apiVersion", "1.0.0"}, {"engineVersion", impl_->version}, {"processMode", impl_->mode},
                   {"capabilities", capabilities}, {"limits", LimitsJson(GetLimits())}, {"commands", Value(std::move(catalog))}});
}

Response DeveloperApi::Submit(Access access, const std::string& commandName, const Value& body)
{
    try {
        const auto command = std::ranges::find_if(commands, [&](const auto& item) { return commandName == item.name; });
        if (command == std::end(commands))
            Reject("unsupported", "Unknown Developer API command.", 404);
        if (command->permission == Access::control && access != Access::control)
            Reject("forbidden", "The command requires runtime control permission.", 403);
        if (!impl_->Available(*command))
            Reject("unsupported", "This process does not support the command.", 404);
        const auto requestId = Get(body, "requestId");
        const auto id = Text(requestId);
        const auto arguments = Get(body, "arguments");
        if (!body.isObject() || body.getDynamicObject()->getProperties().size() != 2 || !requestId.isString() ||
            !ValidId(id) || !Matches(arguments, Parameters(*command, impl_->settings)))
            Reject("invalid_input", "The request does not match the command contract.");
        const auto fingerprint = Fingerprint(arguments);
        std::lock_guard lock(impl_->mutex);
        impl_->Sweep();
        const auto key = std::make_pair(access, id);
        const auto existing = impl_->jobs.find(key);
        if (existing != impl_->jobs.end()) {
            if (existing->second.command != command || existing->second.fingerprint != fingerprint)
                Reject("conflict", "The request identifier was already used with different arguments.", 409);
            return impl_->Report(existing->second, 202);
        }
        const auto active = std::ranges::count_if(impl_->jobs, [](const auto& entry) { return Impl::Active(entry.second); });
        if (impl_->stopping || active >= GetLimits().outstanding || active - impl_->running >= GetLimits().queueDepth ||
            impl_->jobs.size() >= static_cast<std::size_t>(GetLimits().requestHistory))
            Reject("resource_limit", "The job or request history limit has been reached.", 429);
        Impl::Job job{id, access, command, arguments, fingerprint, Clock::now() + std::chrono::milliseconds(GetLimits().deadlineMs)};
        const auto inserted = impl_->jobs.emplace(key, std::move(job));
        impl_->changed.notify_all();
        return impl_->Report(inserted.first->second, 202);
    } catch (const Failure& failure) {
        return FailureResponse(failure);
    }
}

Response DeveloperApi::Job(Access access, const std::string& id, bool cancel)
{
    try {
        if (!ValidId(id)) Reject("invalid_input", "Invalid job identifier.");
        std::lock_guard lock(impl_->mutex);
        impl_->Sweep();
        const auto found = impl_->jobs.find({access, id});
        if (found == impl_->jobs.end()) Reject("not_found", "No job with this identifier exists for this credential.", 404);
        auto& job = found->second;
        if (!Impl::Active(job) && Clock::now() >= job.expires)
            return impl_->Report(job, 200);
        if (cancel && (job.state == Impl::State::running || (!Impl::Active(job) && job.state != Impl::State::cancelled)))
            Reject("not_cancellable", "The job has passed its cancellation boundary.", 409);
        if (cancel && job.state == Impl::State::queued)
            impl_->Finish(job, Impl::State::cancelled, {}, {});
        return impl_->Report(job, 200);
    } catch (const Failure& failure) {
        return FailureResponse(failure);
    }
}

Value DeveloperApi::OpenApi() const
{
    const auto error = Struct({{"code", Type("string")}, {"message", Type("string")}, {"details", Type("object")}}, {"code", "message"});
    const auto envelope = Struct({{"error", error}}, {"error"});
    const auto discovery = Struct({{"apiVersion", Type("string")}, {"engineVersion", Type("string")},
                                   {"processMode", Choice({"editor", "server"})}, {"capabilities", List(Type("string"))},
                                   {"limits", Type("object")}, {"commands", List(Type("object"))}},
                                  {"apiVersion", "engineVersion", "processMode", "capabilities", "limits", "commands"});
    const auto job = Struct({{"id", RequestId()}, {"command", Type("string")},
                             {"state", Choice({"queued", "running", "succeeded", "failed", "cancelled", "timed_out"})},
                             {"cancellable", Type("boolean")}, {"deadlineExceeded", Type("boolean")}, {"result", Type("object")}, {"error", error}},
                            {"id", "command", "state", "cancellable", "deadlineExceeded"});
    const auto media = [](Value schema) { return Object({{"application/json", Object({{"schema", std::move(schema)}})}}); };
    const auto response = [&](const char* description, Value schema) {
        return Object({{"description", description}, {"content", media(std::move(schema))}});
    };
    const auto endpoint = [&](const char* id, const char* status, Value schema) {
        auto responses = Object();
        Put(responses, status, response("Successful request", std::move(schema)));
        for (const auto& [code, description] : std::initializer_list<std::pair<const char*, const char*>>{
                 {"400", "Invalid input"}, {"401", "Authentication required"}, {"403", "Permission denied"},
                 {"404", "Unavailable or unknown resource"}, {"408", "Deadline reached"}, {"409", "Conflict"},
                 {"410", "Result or retained data expired"}, {"413", "Request too large"}, {"429", "Resource limit"},
                 {"500", "Internal failure"}})
            Put(responses, code, response(description, envelope));
        return Object({{"operationId", id}, {"responses", responses}});
    };
    const auto parameter = [](const char* name, Value schema) {
        return Object({{"name", name}, {"in", "path"}, {"required", true}, {"schema", std::move(schema)}});
    };
    auto submit = endpoint("submitCommand", "202", job);
    juce::Array<Value> names;
    for (const auto& command : commands) names.add(command.name);
    Put(submit, "parameters", Array({parameter("command", Object({{"enum", Value(std::move(names))}}))}));
    Put(submit, "requestBody", Object({{"required", true}, {"content", media(Struct(
        {{"requestId", RequestId()}, {"arguments", Type("object")}}, {"requestId", "arguments"}))}}));
    auto getJob = endpoint("getJob", "200", job);
    auto cancelJob = endpoint("cancelJob", "200", job);
    Put(getJob, "parameters", Array({parameter("id", RequestId())}));
    Put(cancelJob, "parameters", Array({parameter("id", RequestId())}));
    return Object({{"openapi", "3.1.1"}, {"info", Object({{"title", "Djehuti Engine Developer API"}, {"version", "1.0.0"}})},
                   {"security", Array({Object({{"developerBearer", Array()}})})},
                   {"components", Object({{"securitySchemes", Object({{"developerBearer", Object({{"type", "http"},
                        {"scheme", "bearer"}, {"description", "Read or runtime-control credential. See command permissions."}})}})}})},
                   {"paths", Object({{"/v1/discovery", Object({{"get", endpoint("discover", "200", discovery)}})},
                                     {"/v1/openapi", Object({{"get", endpoint("getOpenApi", "200", Type("object"))}})},
                                     {"/v1/commands/{command}", Object({{"post", submit}})},
                                     {"/v1/jobs/{id}", Object({{"get", getJob}, {"delete", cancelJob}})}})},
                   {"x-djehuti-commands", Get(Discovery(Access::control), "commands")}});
}
} // namespace ce::developer
