#pragma once

#include <cmath>
#include <unordered_set>

#include <entt/entt.hpp>
#include <juce_opengl/juce_opengl.h>

#include "Scene/Components.h"

namespace ce::scene {

inline juce::Vector3D<float> MatrixTranslation(const juce::Matrix3D<float>& matrix)
{
    return { matrix.mat[12], matrix.mat[13], matrix.mat[14] };
}

// Convert a world-space point into a parent's local space. This lets editor
// reparenting preserve the object's visible position even when the parent is
// rotated or scaled.
inline juce::Vector3D<float> InverseTransformPoint(const juce::Matrix3D<float>& matrix,
                                                    juce::Vector3D<float> point)
{
    const auto translated = point - MatrixTranslation(matrix);
    const float a = matrix.mat[0], b = matrix.mat[4], c = matrix.mat[8];
    const float d = matrix.mat[1], e = matrix.mat[5], f = matrix.mat[9];
    const float g = matrix.mat[2], h = matrix.mat[6], i = matrix.mat[10];
    const float determinant = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
    if (std::abs(determinant) < 0.000001f)
        return translated;

    const float inv = 1.0f / determinant;
    return { ((e * i - f * h) * translated.x + (c * h - b * i) * translated.y + (b * f - c * e) * translated.z) * inv,
             ((f * g - d * i) * translated.x + (a * i - c * g) * translated.y + (c * d - a * f) * translated.z) * inv,
             ((d * h - e * g) * translated.x + (b * g - a * h) * translated.y + (a * e - b * d) * translated.z) * inv };
}

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
