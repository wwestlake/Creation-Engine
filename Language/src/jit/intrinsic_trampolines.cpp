#include "intrinsic_trampolines.h"

#include <cmath>
#include <cstdint>
#include <iostream>

#include <entt/entt.hpp>

#include "engine/core_components.h"
#include "engine/script_context.h"
#include "engine/world.h"

// Every function here is the real implementation behind an
// intrinsics.def cSymbol entry (World/Entity/Transform/Debug, plus
// atan2) -- extern "C", noexcept, called with World::RegistryMutex()
// already held by the caller (Runtime::RunWorldProgram / the future
// Simulation::Step), per the ABI's rule 6: none of these lock it
// themselves. `ce::engine::ScriptContext*` is always the first
// parameter, matching every CEL function/intrinsic call's implicit
// leading argument (module_builder.cpp).
//
// vec3 arguments/returns cross this boundary as `float*` (3 packed
// floats, x/y/z) rather than by value, per the ABI's rule 1 -- MSVC
// x64's hidden-sret-pointer struct-return convention for a
// call-by-value aggregate isn't something a hand-built LLVM `call`
// instruction can reliably replicate, so the convention is sidestepped
// entirely instead. String arguments cross as an explicit
// (const char*, int64_t length) pair -- CEL strings are literal-only
// (see type.h), so this never needs to represent an owned/heap string.

using ce::engine::ScriptContext;
using ce::engine::Transform;
using ce::engine::Vec3;

namespace {

entt::entity ToEntity(uint64_t id) {
    return static_cast<entt::entity>(static_cast<entt::id_type>(id));
}

uint64_t FromEntity(entt::entity e) {
    return static_cast<uint64_t>(static_cast<entt::id_type>(e));
}

Vec3 LoadVec3(const float* xyz) {
    return Vec3{ xyz[0], xyz[1], xyz[2] };
}

void StoreVec3(float* out, const Vec3& v) {
    out[0] = v.x;
    out[1] = v.y;
    out[2] = v.z;
}

// get_position/get_rotation/get_scale read (and set_* write) a
// Transform that may not exist yet on a given entity -- rather than
// making "every entity always has a Transform" an invariant the whole
// engine must maintain just for scripting's sake, these lazily
// get-or-emplace a default-constructed one, matching how a freshly
// `spawn()`-ed (not `spawn_at`) entity has no meaningful transform
// until a script gives it one.
Transform& GetOrCreateTransform(ScriptContext* ctx, entt::entity e) {
    return ctx->world->Registry().get_or_emplace<Transform>(e);
}

} // namespace

extern "C" {

// --- Math (the one Math intrinsic with no direct LLVM intrinsic) -----

float ce_atan2(ScriptContext*, float y, float x) noexcept {
    return std::atan2(y, x);
}

// --- World -------------------------------------------------------------

int64_t ce_world_tick(ScriptContext* ctx) noexcept {
    return static_cast<int64_t>(ctx->world->CurrentTick());
}

float ce_world_time(ScriptContext* ctx) noexcept {
    return ctx->elapsedTime;
}

// --- Entity --------------------------------------------------------------

uint64_t ce_spawn(ScriptContext* ctx) noexcept {
    entt::entity e = ctx->world->CreateEntity();
    ctx->world->Registry().emplace<Transform>(e);
    return FromEntity(e);
}

uint64_t ce_spawn_at(ScriptContext* ctx, const float* xyz) noexcept {
    entt::entity e = ctx->world->CreateEntity();
    ctx->world->Registry().emplace<Transform>(e, Transform{ LoadVec3(xyz), Vec3{}, Vec3{ 1.0f, 1.0f, 1.0f } });
    return FromEntity(e);
}

void ce_destroy(ScriptContext* ctx, uint64_t entity) noexcept {
    entt::entity e = ToEntity(entity);
    if (ctx->world->Registry().valid(e)) {
        ctx->world->DestroyEntity(e);
    }
}

int32_t ce_is_valid(ScriptContext* ctx, uint64_t entity) noexcept {
    return ctx->world->Registry().valid(ToEntity(entity)) ? 1 : 0;
}

int64_t ce_entity_count(ScriptContext* ctx) noexcept {
    // basic_registry::storage<entity_type>() (non-const) returns the
    // entity pool itself BY REFERENCE (not a pointer -- unlike every
    // component-type storage lookup), always valid, creating it if this
    // is a brand-new World that has never created an entity. size()
    // includes recycled/tombstoned slots; free_list() is exactly that
    // recyclable count, so the difference is the number of entities
    // genuinely alive right now -- the same arithmetic
    // basic_registry::valid() itself uses internally.
    auto& pool = ctx->world->Registry().storage<entt::entity>();
    return static_cast<int64_t>(pool.size() - pool.free_list());
}

uint64_t ce_find_by_name(ScriptContext*, const char*, int64_t) noexcept {
    // No Name component exists in EngineCore yet (ce::scene::Name is
    // JUCE-facing, Source/Scene/Components.h, unreachable from here) --
    // stubbed to "always not found" rather than blocking the rest of
    // GS5's ABI on a name-lookup system this milestone's verification
    // scripts don't need. A real implementation is a follow-up once
    // EngineCore grows a framework-agnostic Name component.
    return FromEntity(entt::null);
}

// --- Transform -------------------------------------------------------

void ce_get_position(ScriptContext* ctx, uint64_t entity, float* outXyz) noexcept {
    StoreVec3(outXyz, GetOrCreateTransform(ctx, ToEntity(entity)).position);
}

void ce_set_position(ScriptContext* ctx, uint64_t entity, const float* xyz) noexcept {
    GetOrCreateTransform(ctx, ToEntity(entity)).position = LoadVec3(xyz);
}

void ce_get_rotation(ScriptContext* ctx, uint64_t entity, float* outXyz) noexcept {
    StoreVec3(outXyz, GetOrCreateTransform(ctx, ToEntity(entity)).eulerRotationRadians);
}

void ce_set_rotation(ScriptContext* ctx, uint64_t entity, const float* xyz) noexcept {
    GetOrCreateTransform(ctx, ToEntity(entity)).eulerRotationRadians = LoadVec3(xyz);
}

void ce_get_scale(ScriptContext* ctx, uint64_t entity, float* outXyz) noexcept {
    StoreVec3(outXyz, GetOrCreateTransform(ctx, ToEntity(entity)).scale);
}

void ce_set_scale(ScriptContext* ctx, uint64_t entity, const float* xyz) noexcept {
    GetOrCreateTransform(ctx, ToEntity(entity)).scale = LoadVec3(xyz);
}

// --- Debug -----------------------------------------------------------

void ce_log(ScriptContext*, const char* msg, int64_t msgLen) noexcept {
    std::cout << "[cel] " << std::string(msg, static_cast<size_t>(msgLen)) << std::endl;
}

void ce_log_int(ScriptContext*, const char* msg, int64_t msgLen, int64_t value) noexcept {
    std::cout << "[cel] " << std::string(msg, static_cast<size_t>(msgLen)) << value << std::endl;
}

void ce_log_float(ScriptContext*, const char* msg, int64_t msgLen, float value) noexcept {
    std::cout << "[cel] " << std::string(msg, static_cast<size_t>(msgLen)) << value << std::endl;
}

void ce_log_vec3(ScriptContext*, const char* msg, int64_t msgLen, const float* xyz) noexcept {
    std::cout << "[cel] " << std::string(msg, static_cast<size_t>(msgLen)) << "(" << xyz[0] << ", " << xyz[1] << ", "
               << xyz[2] << ")" << std::endl;
}

// --- Watchdog ----------------------------------------------------------

// Called once per loop iteration entered (module_builder.cpp inserts
// this at the top of every while/for body, i.e. every loop back-edge).
// Returns 0 ("abort") once the per-context budget is exhausted or the
// context is already faulted; the caller (generated IR) reacts by
// returning from the CURRENT function immediately -- a simple,
// non-exception-based unwind that's enough to stop a runaway script
// without needing SEH across JIT'd frames.
int32_t ce_watchdog_tick(ScriptContext* ctx) noexcept {
    if (ctx->faulted) {
        return 0;
    }
    if (ctx->loopBudget <= 0) {
        ctx->faulted = true;
        ctx->faultMessage = "CEL9001: script exceeded its loop-iteration budget (possible infinite loop)";
        return 0;
    }
    --ctx->loopBudget;
    return 1;
}

} // extern "C"

namespace ce::lang::jit {

std::vector<AbiSymbol> GetAbiTrampolines() {
    return {
        { "ce_atan2", reinterpret_cast<void*>(&ce_atan2) },
        { "ce_world_tick", reinterpret_cast<void*>(&ce_world_tick) },
        { "ce_world_time", reinterpret_cast<void*>(&ce_world_time) },
        { "ce_spawn", reinterpret_cast<void*>(&ce_spawn) },
        { "ce_spawn_at", reinterpret_cast<void*>(&ce_spawn_at) },
        { "ce_destroy", reinterpret_cast<void*>(&ce_destroy) },
        { "ce_is_valid", reinterpret_cast<void*>(&ce_is_valid) },
        { "ce_entity_count", reinterpret_cast<void*>(&ce_entity_count) },
        { "ce_find_by_name", reinterpret_cast<void*>(&ce_find_by_name) },
        { "ce_get_position", reinterpret_cast<void*>(&ce_get_position) },
        { "ce_set_position", reinterpret_cast<void*>(&ce_set_position) },
        { "ce_get_rotation", reinterpret_cast<void*>(&ce_get_rotation) },
        { "ce_set_rotation", reinterpret_cast<void*>(&ce_set_rotation) },
        { "ce_get_scale", reinterpret_cast<void*>(&ce_get_scale) },
        { "ce_set_scale", reinterpret_cast<void*>(&ce_set_scale) },
        { "ce_log", reinterpret_cast<void*>(&ce_log) },
        { "ce_log_int", reinterpret_cast<void*>(&ce_log_int) },
        { "ce_log_float", reinterpret_cast<void*>(&ce_log_float) },
        { "ce_log_vec3", reinterpret_cast<void*>(&ce_log_vec3) },
        { "ce_watchdog_tick", reinterpret_cast<void*>(&ce_watchdog_tick) },
    };
}

std::unordered_map<std::string, IntrinsicDomain> GetAbiSymbolDomains() {
    std::unordered_map<std::string, IntrinsicDomain> domains;
// Keyed by cSymbol (not `name`) to match GetAbiTrampolines()'s own key
// space -- RegisterAbiTrampolines looks up each AbiSymbol::name (which
// IS the cSymbol) against this map. The pure-IR math/vec3 intrinsics'
// cSymbol is an inert placeholder (their own name, per intrinsics.def's
// own comment) and never appears in GetAbiTrampolines()'s real list, so
// those entries here are simply never queried -- harmless, not a bug.
#define CEL_INTRINSIC0(name, cSymbol, purity, domain, ret) domains[#cSymbol] = IntrinsicDomain::domain;
#define CEL_INTRINSIC1(name, cSymbol, purity, domain, ret, p1) domains[#cSymbol] = IntrinsicDomain::domain;
#define CEL_INTRINSIC2(name, cSymbol, purity, domain, ret, p1, p2) domains[#cSymbol] = IntrinsicDomain::domain;
#define CEL_INTRINSIC3(name, cSymbol, purity, domain, ret, p1, p2, p3) domains[#cSymbol] = IntrinsicDomain::domain;
#include "lang/intrinsics.def"
#undef CEL_INTRINSIC0
#undef CEL_INTRINSIC1
#undef CEL_INTRINSIC2
#undef CEL_INTRINSIC3
    return domains;
}

} // namespace ce::lang::jit
