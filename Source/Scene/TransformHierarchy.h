#pragma once

#include <unordered_set>

#include <entt/entt.hpp>
#include <juce_opengl/juce_opengl.h>

#include "Scene/Components.h"

namespace ce::scene {

// Resolves the model matrix from the entity's local Transform and its Parent
// chain. Folders are transparent transform parents: they organize the tree,
// but do not introduce a coordinate space of their own.
inline juce::Matrix3D<float> WorldModelMatrix(const entt::registry& registry, entt::entity entity)
{
    const juce::Matrix3D<float> identity;
    std::unordered_set<entt::entity> visited;

    const auto resolve = [&](auto&& self, entt::entity current) -> juce::Matrix3D<float> {
        if (current == entt::null || !registry.valid(current) || !visited.insert(current).second)
            return identity;

        const auto* local = registry.try_get<const Transform>(current);
        const auto localMatrix = local != nullptr ? ToModelMatrix(*local) : identity;
        const auto* parent = registry.try_get<const Parent>(current);
        if (parent == nullptr || parent->value == entt::null)
            return localMatrix;

        return self(self, parent->value) * localMatrix;
    };

    return resolve(resolve, entity);
}

} // namespace ce::scene
