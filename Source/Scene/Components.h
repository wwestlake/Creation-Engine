#pragma once

#include <memory>
#include <vector>

#include <entt/entt.hpp>
#include <JuceHeader.h>

#include "Render/Scene/Material.h"
#include "Render/Scene/Mesh.h"

namespace ce::scene {

// This folder (Source/Scene/) is the editable scene composition layer —
// entities placed by a designer, living in the same ce::engine::World
// the game itself will simulate against. It's distinct from
// Source/Render/Scene/, which holds rendering-primitive types (Mesh,
// Material, Camera, Light) with no notion of "a scene" at all.

struct Name {
    juce::String value;
};

struct Transform {
    juce::Vector3D<float> position;
    juce::Vector3D<float> eulerRotationRadians;
    juce::Vector3D<float> scale{ 1.0f, 1.0f, 1.0f };

    juce::Matrix3D<float> ToModelMatrix() const {
        const auto translation = juce::Matrix3D<float>::fromTranslation(position);
        const auto rotation = juce::Matrix3D<float>::rotation(eulerRotationRadians);
        const juce::Matrix3D<float> scaling(scale.x, 0.0f, 0.0f, 0.0f, 0.0f, scale.y, 0.0f, 0.0f, 0.0f, 0.0f, scale.z,
                                             0.0f, 0.0f, 0.0f, 0.0f, 1.0f);
        return translation * rotation * scaling;
    }
};

// Non-owning references to catalog assets (AssetCatalog.h owns the
// shared_ptr originals) — several entities can point at the same Mesh/
// Material without duplicating GPU resources.
struct MeshRenderer {
    std::shared_ptr<Mesh> mesh;
    std::shared_ptr<Material> material;
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

// Per-entity editor flags, attached to every entity/folder at creation
// time (so nothing has to special-case "missing SceneFlags = default").
// Flat, not cascading — locking or hiding a folder doesn't currently
// affect its children; that's a reasonable follow-up, not implemented
// here since it wasn't asked for.
struct SceneFlags {
    bool visible = true; // false: ViewportComponent skips drawing this entity.
    bool locked = false; // true: this entity can't be reparented (or, once SC4 exists, transform-edited) in the editor.
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
    };
    std::vector<Joint> joints;
};

} // namespace ce::scene
