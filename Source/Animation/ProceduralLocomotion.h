#pragma once

#include <cstdint>

namespace ce::animation
{
// Deliberately independent of renderer and physics-library vector types. This
// keeps locomotion calculations deterministic and directly unit-testable.
struct Vec3
{
    float x{};
    float y{};
    float z{};

    [[nodiscard]] float length() const;
    [[nodiscard]] float lengthSquared() const;
    [[nodiscard]] Vec3 normalized(float epsilon = 1.0e-5f) const;
};

[[nodiscard]] Vec3 operator+(Vec3 left, Vec3 right);
[[nodiscard]] Vec3 operator-(Vec3 left, Vec3 right);
[[nodiscard]] Vec3 operator-(Vec3 value);
[[nodiscard]] Vec3 operator*(Vec3 value, float scalar);
[[nodiscard]] Vec3 operator*(float scalar, Vec3 value);
[[nodiscard]] Vec3 operator/(Vec3 value, float scalar);
[[nodiscard]] float Dot(Vec3 left, Vec3 right);
[[nodiscard]] Vec3 Cross(Vec3 left, Vec3 right);

struct Quaternion
{
    float w{ 1.0f };
    float x{};
    float y{};
    float z{};

    [[nodiscard]] Quaternion normalized() const;
    [[nodiscard]] Vec3 rotate(Vec3 value) const;

    static Quaternion fromAxisAngle(Vec3 axis, float radians);
    static Quaternion fromTo(Vec3 from, Vec3 to);
};

struct Transform
{
    Vec3 position{};
    Quaternion rotation{};
};

enum class GaitSide
{
    left,
    right
};

struct FootstepPlanInput
{
    Vec3 centreOfMass{};
    Vec3 actualPlanarVelocity{};
    Vec3 desiredPlanarVelocity{};
    Vec3 facingForward{ 0.0f, 0.0f, -1.0f };
    Vec3 worldUp{ 0.0f, 1.0f, 0.0f };
    float gravity{ 9.81f };
    float comHeight{ 1.0f };
    float stepDuration{ 0.45f };
    float stanceWidth{ 0.20f };
    float velocityCorrectionGain{ 0.18f };
    GaitSide swingSide{ GaitSide::left };
};

struct FootstepPlan
{
    Vec3 predictedLanding{};
    float naturalFrequency{};
};

// A compact LIPM/capture-point target. Collision queries are deliberately
// applied afterwards: this is a physically motivated proposal, not authority
// to move through terrain or walls.
[[nodiscard]] FootstepPlan PlanFootstep(const FootstepPlanInput& input);

[[nodiscard]] Vec3 EvaluateSwingArc(Vec3 start, Vec3 target, Vec3 worldUp,
                                     float normalizedPhase, float clearance);

struct ContactAnchor
{
    std::uint64_t supportId{};
    Vec3 localPosition{};
    Vec3 localNormal{ 0.0f, 1.0f, 0.0f };
};

[[nodiscard]] ContactAnchor MakeContactAnchor(std::uint64_t supportId, const Transform& supportWorld,
                                               Vec3 worldPosition, Vec3 worldNormal);
[[nodiscard]] Vec3 ResolveContactPosition(const ContactAnchor& anchor, const Transform& supportWorld);
[[nodiscard]] Vec3 ResolveContactNormal(const ContactAnchor& anchor, const Transform& supportWorld);

struct TwoBoneIKInput
{
    Vec3 rootPosition{};
    Vec3 targetPosition{};
    Vec3 poleDirection{ 0.0f, 0.0f, 1.0f };
    Vec3 preferredPelvisUp{ 0.0f, 1.0f, 0.0f };
    float upperLength{};
    float lowerLength{};
};

struct TwoBoneIKResult
{
    Vec3 midPosition{};
    Vec3 endPosition{};
    float requestedDistance{};
    float solvedDistance{};
    float requiredPelvisDrop{};
    bool reachableWithoutPelvisAdjustment{};
};

// Returns a no-stretch target even when the requested end effector cannot be
// reached. The caller may use requiredPelvisDrop as an input to a later,
// two-leg pelvis solve; this function never secretly moves the body root.
[[nodiscard]] TwoBoneIKResult SolveTwoBoneIK(const TwoBoneIKInput& input);

// Produces the minimal rotation which maps a foot's bind-pose up vector to the
// sampled ground normal. Clamp handling belongs to the rig-profile layer.
[[nodiscard]] Quaternion FootOrientationForGround(Vec3 footUp, Vec3 groundNormal);

} // namespace ce::animation
