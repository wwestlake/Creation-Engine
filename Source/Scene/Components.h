#pragma once

#include <memory>
#include <vector>

#include <creation/assets/AssetTypes.h>
#include <entt/entt.hpp>
#include <juce_core/juce_core.h>
#include <juce_graphics/juce_graphics.h>
#include <juce_opengl/juce_opengl.h>

#include "engine/core_components.h"
#include "engine/math.h"
#include "Render/Scene/Animation.h"

namespace ce
{
class Material;
class Mesh;
}

namespace ce::scene {

// This folder (Source/Scene/) is the editable scene composition layer —
// entities placed by a designer, living in the same ce::engine::World
// the game itself will simulate against. It's distinct from
// Source/Render/Scene/, which holds rendering-primitive types (Mesh,
// Material, Camera, Light) with no notion of "a scene" at all.

struct Name {
    juce::String value;
};

// Scene/Game VFS Rearchitecture plan, Phase 1: a stable, persistent identity
// that survives across saves -- unlike the entt::entity handle (a live,
// per-session ECS identity that entt is free to reuse across
// create/destroy), this is minted exactly once, when an entity is first
// placed into a scene, and never regenerated. Two jobs: (1) the VFS
// filename segment for this entity's own instance entry once scene storage
// stops being one serialized blob per scene, (2) the serialized
// Parent-reference cross-key, replacing the ephemeral numeric entt id
// EngineSceneSerializer uses for that today (which only round-trips
// correctly within a single save/load pair). Every entity that persists
// into a saved scene gets one; the synthetic SceneRoot and purely
// transient entities (e.g. a possessed character, destroyed on Stop) don't
// need one and don't get one.
struct InstanceId {
    juce::String value;
};

// The editor and runtime share this component type, so inspector changes and
// host capability changes update the same EnTT component pool.
using Transform = engine::Transform;

inline juce::Vector3D<float> ToJuceVector3D(const engine::Vec3& v) { return { v.x, v.y, v.z }; }
inline engine::Vec3 ToVec3(const juce::Vector3D<float>& v) { return { v.x, v.y, v.z }; }

// Replaces the old Transform::ToModelMatrix() method -- ce::engine::Transform
// is a plain framework-agnostic POD with no JUCE-returning methods of its
// own, so the JUCE-specific matrix composition lives here instead, at
// the boundary where it's actually needed (ViewportComponent's render
// loop).
inline juce::Matrix3D<float> ToModelMatrix(const Transform& t) {
    const auto translation = juce::Matrix3D<float>::fromTranslation(ToJuceVector3D(t.position));
    const auto rotation = juce::Matrix3D<float>::rotation(ToJuceVector3D(t.eulerRotationRadians));
    const juce::Matrix3D<float> scaling(t.scale.x, 0.0f, 0.0f, 0.0f, 0.0f, t.scale.y, 0.0f, 0.0f, 0.0f, 0.0f, t.scale.z,
                                         0.0f, 0.0f, 0.0f, 0.0f, 1.0f);
    return translation * rotation * scaling;
}

// Non-owning references to catalog assets (AssetCatalog.h owns the
// shared_ptr originals) — several entities can point at the same Mesh/
// Material without duplicating GPU resources.
struct MeshRenderer {
    std::shared_ptr<Mesh> mesh;
    std::shared_ptr<Material> material;
};

// A serializable asset identifier. Object definitions use this instead of
// embedding GPU resources, and the viewport resolves it through AssetCatalog.
struct MeshAssetReference {
    juce::String assetId;
    juce::String versionId;
    juce::String packId;
    juce::String packVersion;

    // Which part of the source model this reference addresses -- mirrors
    // ObjectComponentEntry's meshNodeIndex/meshNodeName (ObjectDefinitions.h)
    // exactly. -1 = whole model / first primitive (today's single-mesh
    // behavior). Carried here too, not just on the definition, because
    // ViewportComponent::ResolveProjectAssets resolves from the live
    // entity's component, not the original definition.
    int nodeIndex = -1;
    juce::String nodeName;

    // Scene/Game VFS Rearchitecture plan: which of AssetResolver's three
    // modes (shared/AssetSystem/include/creation/assets/AssetTypes.h) this
    // instance wants -- exact (default, pinned to versionId forever,
    // today's actual always-pinned behavior exactly), latest (always
    // tracks the newest version), or compatibleLatest (prefers versionId,
    // falls back if it's gone). AssetResolver::resolve already implements
    // all three correctly and is already tested (AssetSystemSmoke.cpp) --
    // this field only gives a scene instance somewhere to record which one
    // it wants; a confirmed, real gap (every actual resolution path
    // hardcoded `exact`, so `latest`/`compatibleLatest` were unreachable
    // from a placed instance despite being fully built). Storage only for
    // now -- ResolveProjectAssets (ViewportComponent.cpp) still always
    // resolves by the exact stored assetId/versionId; wiring it to
    // actually re-resolve via AssetResolver::resolve for latest/
    // compatibleLatest is separate, not-yet-scoped follow-up work, not
    // silently claimed done by this field's existence alone.
    creation::assets::AssetReferenceMode referenceMode = creation::assets::AssetReferenceMode::exact;
};

// Identity and authored behavior remain on the entity rather than inside a
// FRust runtime, so projects can inspect and save them independently.
struct ObjectDefinitionRef {
    juce::String definitionId;
};

struct BehaviorAttachments {
    std::vector<juce::String> podIds;
};

// A Scene owns instances, never the character asset itself. The referenced
// Definition is a suite-wide VFS asset; the rest is durable state belonging
// to this particular placed or player-created character.
struct CharacterInstanceRef {
    juce::String instanceId;
    juce::String definitionAssetId;
    juce::String definitionVersionId;
    juce::String rosterAssetId;
    juce::NamedValueSet state;
};

// Engine-provided scene objects are authored entities, not project assets.
// Their visualization is editor-only, while their configuration remains
// available to the running scene.
enum class BuiltInKind : std::uint8_t {
    spawner,
    playerStart,
    cameraMarker,
    triggerVolume,
    waypoint,
    audioEmitter
};

struct SceneBuiltIn {
    BuiltInKind kind = BuiltInKind::spawner;
};

// Player Start is one configured use of this durable generic spawner.
// NPC, loot, vehicle, and scripted encounter spawners will use the same
// contract without making Player Start a special asset type.
struct Spawner {
    juce::String spawnId;
    bool enabled = true;
    int maximumActive = 1;
    float respawnDelaySeconds = 0.0f;
};

// A named place where a Game's PlayerSlot may create or restore its selected
// CharacterInstance. The slot name, rather than an EnTT handle, is the stable
// connection between a Game asset and a Scene asset.
struct PossessionSpawn {
    juce::String playerSlotId = "player-1";
    // Authored character asset to instantiate when this Player Start is used.
    // The spawned entity is runtime-only; the marker remains the sole scene
    // object that records the player's initial character choice.
    juce::String characterAssetId;
};

// Runtime-only identity for a character created by a PossessionSpawn. It is
// deliberately not serialized: Stop restores the authored scene snapshot.
struct RuntimeSpawnedCharacter {
    juce::String playerSlotId;
};

// Compatibility reader for scenes saved before PossessionSpawn existed.
// New authoring must use CharacterInstanceRef + PossessionSpawn; the old
// marker remains only so existing project scenes keep opening correctly.
struct PlayerSpawn {
    float capsuleRadiusMeters = 0.3f;
    float capsuleHalfHeightMeters = 0.9f;
};

// Animation Control plan Phase 3: freezes this entity's own attached
// Pods -- EngineFrustHost::tick skips on_tick for an entity carrying this
// (lifecycle hooks on_spawn/on_begin_play/on_end_play still fire once as
// normal, so pausing/resuming never re-runs or skips one-time setup) --
// while the rest of the world keeps simulating normally. Answers "hard to
// work on an NPC that's trying to kill things": freeze just its decision-
// making, not its rendering or its already-commanded animation (which
// keeps sampling in ViewportComponent regardless, per Animator's own
// playbackSpeed/blend fields further below). Same bool-equivalent marker shape as
// SceneFlags::editorOnly below -- not a data-carrying struct because
// there's nothing to carry, just presence/absence.
struct BehaviorPaused {};

struct ObjectState {
    juce::NamedValueSet values;
};

// Hierarchy grouping (spec section 3.2: "organize placed objects into a
// scene hierarchy/grouping structure"). entt::null means "at the root."
// Any entity can have a Parent — folders (below) are the common case,
// but nothing stops a future feature from nesting an entity under
// another entity directly.
struct Parent {
    entt::entity value = entt::null;
};

// Marks an entity as a pure organizational folder: it has a Name and
// optionally a Parent, but no Transform/MeshRenderer — it's never drawn,
// only used to group other entities in the hierarchy panel.
struct Folder {};

// Marks the one entity that IS the currently-loaded Scene itself -- a real
// Thing with its own metric (Transform), not a privileged engine concept
// (see docs/OBJECT_MODEL.md's "Entity, Thing, Object" section). Every
// entity with no Parent of its own is positioned relative to this one's
// Transform, exactly the same "houses" composition any other parent
// entity provides -- a Scene is not a special case of the hierarchy, it's
// just the one entity everything else in it, directly or indirectly,
// traces its Parent chain back to.
//
// Synthesized fresh by EngineGameDocumentStore::loadScene on every load,
// never persisted (EngineSceneSerializer::serializeScene skips entities
// carrying this) -- there is exactly one per loaded World, always
// reconstructible from the SceneDocumentInfo already being loaded, so
// there's nothing here worth the round-trip complexity of persisting and
// re-detecting it.
struct SceneRoot {
    juce::String sceneId;
};

// Per-entity editor flags, attached to every entity/folder at creation
// time (so nothing has to special-case "missing SceneFlags = default").
// Flat, not cascading — locking or hiding a folder doesn't currently
// affect its children; that's a reasonable follow-up, not implemented
// here since it wasn't asked for.
struct SceneFlags {
    bool visible = true; // false: ViewportComponent skips drawing this entity.
    bool locked = false; // true: this entity can't be reparented (or, once SC4 exists, transform-edited) in the editor.
    // true: this entity exists only for the privileged editor experience
    // (e.g. the VR edit-mode cart) and must not render or be pickable
    // while Play is active -- hidden, not despawned, so re-entering the
    // editor brings it straight back with no respawn logic needed.
    // VR Editor Cart plan Phase 2.
    bool editorOnly = false;
};

// AI4: a skinned mesh's bind-pose skeleton, flattened into a
// cache-friendly array rather than the node-graph shape glTF (or any
// other format) represents it as -- deliberately its own type, not a
// reuse of Render/Import/GltfLoader.h's LoadedSkin/LoadedJoint, so this
// component doesn't depend on which importer happened to produce it.
// Attached alongside MeshRenderer on entities placed from a skinned
// catalog asset (see AssetCatalog::AddFromModel); simply absent on
// non-skinned entities, same "missing component = doesn't apply"
// convention the rest of this file already uses.
//
// AI4 only ever displays the bind pose (localBindTransform as-is, no
// animation). AI5's animation playback will walk the same parentIndex
// hierarchy but compose ANIMATED local transforms instead of these bind
// ones -- inverseBindMatrix is what makes either pose actually skin the
// mesh correctly (mesh-space -> joint-space at bind time, composed with
// the joint's current -- bind or animated -- world matrix).
struct Skeleton {
    struct Joint {
        juce::String name;
        int parentIndex = -1; // index into Skeleton::joints, or -1 for a root joint.
        juce::Matrix3D<float> inverseBindMatrix;
        juce::Matrix3D<float> localBindTransform;

        // AI5: bind pose decomposed into TRS (see LoadedJoint's matching
        // fields in Render/Import/GltfLoader.h for why) -- the fallback
        // an animation channel that doesn't touch this joint, or a joint
        // path a clip never animates, keeps instead of snapping to
        // identity translation/rotation/scale.
        juce::Vector3D<float> bindTranslation;
        float bindRotation[4] = { 0.0f, 0.0f, 0.0f, 1.0f }; // x, y, z, w.
        juce::Vector3D<float> bindScale{ 1.0f, 1.0f, 1.0f };
    };
    std::vector<Joint> joints;
};

// AI5: per-instance animation playback state for an entity that also has
// a Skeleton. `clips` is shared (shared_ptr, not copied) across every
// placed instance of the same source asset -- the clip DATA never
// differs per instance, same reasoning as MeshRenderer's shared Mesh/
// Material, only where playback currently is does.
struct Animator {
    std::shared_ptr<std::vector<AnimationClip>> clips;
    int activeClip = -1; // index into *clips, or -1 for "no clip / bind pose."
    float time = 0.0f;   // seconds into the active clip.
    bool playing = false;
    bool loop = true;
    float playbackSpeed = 1.0f; // multiplies deltaSeconds before advancing `time`.

    // AI7 animation-control crossfade: when blendFromClip >= 0, the render
    // path samples BOTH blendFromClip (at its own advancing `time` from
    // the crossfade started) and activeClip (the blend target), blending
    // toward activeClip as blendTime advances toward blendDuration. Once
    // blendTime >= blendDuration the blend is done and blendFromClip resets
    // to -1 -- ordinary single-clip sampling resumes. Set by a Pod's
    // animCrossfadeTo host node (Source/Frust/EngineFrustHost), not by
    // anything in the render path itself.
    int blendFromClip = -1;
    float blendFromTime = 0.0f; // seconds into blendFromClip; advances while the crossfade is active.
    float blendTime = 0.0f;
    float blendDuration = 0.0f;
};

} // namespace ce::scene
