#pragma once

namespace ce::engine {

// Engine Loop Decoupling plan, Phase 1: names the ordering
// MainComponent::timerCallback() (and, eventually, CreationEngineServer's
// own main loop -- Simulation::Step already runs identically in both, per
// that class's own header comment) already implicitly follows today, as a
// real, explicit contract instead of "whatever order the function happens
// to call things in." Deliberately small -- 3 phases, not Unreal Engine's
// full tick-group set (TG_PrePhysics/TG_StartPhysics/TG_DuringPhysics/
// TG_EndPhysics/TG_PostPhysics/TG_PostUpdateWork/...), researched directly
// against Epic's own source for this plan. Nothing here needs that much
// granularity yet -- add a phase only once a real case needs the extra
// ordering point, not speculatively.
enum class EngineTickPhase {
    // Pod on_tick, input polling, anything that reads/writes gameplay
    // state and DECLARES movement/force intent -- never reads this same
    // tick's not-yet-computed physics results.
    PreUpdate,

    // PhysicsWorld::Advance() resolving this tick's fixed physics step(s)
    // -- settles the intents PreUpdate declared into real positions/
    // contacts. Not a phase gameplay code runs IN; it's the boundary
    // between PreUpdate and PostPhysics.
    PhysicsResolve,

    // Possession/camera update, collision-event dispatch to Pods -- runs
    // after this tick's physics is settled, safe to read this same tick's
    // fresh results (a collision that just happened, a character's
    // just-resolved position).
    PostPhysics,
};

} // namespace ce::engine
