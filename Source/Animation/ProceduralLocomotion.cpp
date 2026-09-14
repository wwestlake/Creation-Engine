#include "Animation/ProceduralLocomotion.h"

#include <algorithm>
#include <cmath>

namespace ce::animation
{
namespace
{
constexpr float epsilon = 1.0e-5f;
constexpr float pi = 3.14159265358979323846f;

float clamp(float value, float minimum, float maximum)
{
    return std::max(minimum, std::min(value, maximum));
}

Vec3 orthogonalUnit(Vec3 direction)
{
    const Vec3 candidate = std::abs(direction.y) < 0.9f ? Cross(direction, { 0.0f, 1.0f, 0.0f })
                                                        : Cross(direction, { 1.0f, 0.0f, 0.0f });
    return candidate.normalized();
}
}

float Vec3::length() const { return std::sqrt(lengthSquared()); }
float Vec3::lengthSquared() const { return Dot(*this, *this); }
Vec3 Vec3::normalized(float minimumLength) const
{
    const auto magnitude = length();
    return magnitude > minimumLength ? *this / magnitude : Vec3{};
}

Vec3 operator+(Vec3 left, Vec3 right) { return { left.x + right.x, left.y + right.y, left.z + right.z }; }
Vec3 operator-(Vec3 left, Vec3 right) { return { left.x - right.x, left.y - right.y, left.z - right.z }; }
Vec3 operator-(Vec3 value) { return { -value.x, -value.y, -value.z }; }
Vec3 operator*(Vec3 value, float scalar) { return { value.x * scalar, value.y * scalar, value.z * scalar }; }
Vec3 operator*(float scalar, Vec3 value) { return value * scalar; }
Vec3 operator/(Vec3 value, float scalar) { return value * (1.0f / scalar); }
float Dot(Vec3 left, Vec3 right) { return left.x * right.x + left.y * right.y + left.z * right.z; }
Vec3 Cross(Vec3 left, Vec3 right)
{
    return { left.y * right.z - left.z * right.y, left.z * right.x - left.x * right.z,
             left.x * right.y - left.y * right.x };
}

Quaternion Quaternion::normalized() const
{
    const auto magnitude = std::sqrt(w * w + x * x + y * y + z * z);
    return magnitude > epsilon ? Quaternion{ w / magnitude, x / magnitude, y / magnitude, z / magnitude }
                               : Quaternion{};
}

Vec3 Quaternion::rotate(Vec3 value) const
{
    const auto unit = normalized();
    const Vec3 imaginary{ unit.x, unit.y, unit.z };
    return value + 2.0f * Cross(imaginary, Cross(imaginary, value) + unit.w * value);
}

Quaternion Quaternion::fromAxisAngle(Vec3 axis, float radians)
{
    const auto unitAxis = axis.normalized();
    if (unitAxis.lengthSquared() < epsilon)
        return {};
    const auto halfAngle = radians * 0.5f;
    const auto sine = std::sin(halfAngle);
    return { std::cos(halfAngle), unitAxis.x * sine, unitAxis.y * sine, unitAxis.z * sine };
}

Quaternion Quaternion::fromTo(Vec3 from, Vec3 to)
{
    const auto source = from.normalized();
    const auto destination = to.normalized();
    if (source.lengthSquared() < epsilon || destination.lengthSquared() < epsilon)
        return {};

    const auto cosine = clamp(Dot(source, destination), -1.0f, 1.0f);
    if (cosine > 1.0f - epsilon)
        return {};
    if (cosine < -1.0f + epsilon)
        return fromAxisAngle(orthogonalUnit(source), pi);

    const auto axis = Cross(source, destination);
    return Quaternion{ 1.0f + cosine, axis.x, axis.y, axis.z }.normalized();
}

FootstepPlan PlanFootstep(const FootstepPlanInput& input)
{
    const auto up = input.worldUp.normalized();
    const auto height = std::max(input.comHeight, 0.05f);
    const auto gravity = std::max(input.gravity, 0.01f);
    const auto omega = std::sqrt(gravity / height);
    const auto duration = std::max(input.stepDuration, 0.01f);
    const auto omegaTime = omega * duration;
    const auto elevation = Dot(input.centreOfMass, up);
    const auto planarPosition = input.centreOfMass - up * elevation;
    const auto actualVelocity = input.actualPlanarVelocity - up * Dot(input.actualPlanarVelocity, up);
    const auto desiredVelocity = input.desiredPlanarVelocity - up * Dot(input.desiredPlanarVelocity, up);
    const auto predicted = planarPosition * std::cosh(omegaTime)
                         + actualVelocity * (std::sinh(omegaTime) / omega) + up * elevation;
    const auto velocityCorrection = (desiredVelocity - actualVelocity)
                                  * input.velocityCorrectionGain;
    const auto forward = input.facingForward.normalized();
    const auto right = Cross(forward, up).normalized();
    const auto side = input.swingSide == GaitSide::left ? -1.0f : 1.0f;
    return { predicted + velocityCorrection + right * (side * input.stanceWidth * 0.5f), omega };
}

Vec3 EvaluateSwingArc(Vec3 start, Vec3 target, Vec3 worldUp, float normalizedPhase, float clearance)
{
    const auto phase = clamp(normalizedPhase, 0.0f, 1.0f);
    const auto smooth = phase * phase * (3.0f - 2.0f * phase);
    const auto arc = 4.0f * clearance * phase * (1.0f - phase);
    return start * (1.0f - smooth) + target * smooth + worldUp.normalized() * arc;
}

ContactAnchor MakeContactAnchor(std::uint64_t supportId, const Transform& supportWorld,
                                Vec3 worldPosition, Vec3 worldNormal)
{
    const auto inverse = Quaternion{ supportWorld.rotation.w, -supportWorld.rotation.x,
                                     -supportWorld.rotation.y, -supportWorld.rotation.z }.normalized();
    return { supportId, inverse.rotate(worldPosition - supportWorld.position), inverse.rotate(worldNormal).normalized() };
}

Vec3 ResolveContactPosition(const ContactAnchor& anchor, const Transform& supportWorld)
{
    return supportWorld.position + supportWorld.rotation.rotate(anchor.localPosition);
}

Vec3 ResolveContactNormal(const ContactAnchor& anchor, const Transform& supportWorld)
{
    return supportWorld.rotation.rotate(anchor.localNormal).normalized();
}

TwoBoneIKResult SolveTwoBoneIK(const TwoBoneIKInput& input)
{
    const auto upperLength = std::max(input.upperLength, epsilon);
    const auto lowerLength = std::max(input.lowerLength, epsilon);
    const auto requestedOffset = input.targetPosition - input.rootPosition;
    const auto requestedDistance = requestedOffset.length();
    const auto reachMinimum = std::abs(upperLength - lowerLength) + epsilon;
    const auto reachMaximum = std::max(reachMinimum, upperLength + lowerLength - epsilon);
    const auto solvedDistance = clamp(requestedDistance, reachMinimum, reachMaximum);
    const auto direction = requestedDistance > epsilon ? requestedOffset / requestedDistance
                                                       : input.preferredPelvisUp.normalized();

    Vec3 bendDirection = input.poleDirection - direction * Dot(input.poleDirection, direction);
    bendDirection = bendDirection.normalized();
    if (bendDirection.lengthSquared() < epsilon)
        bendDirection = orthogonalUnit(direction);

    const auto hipCosine = clamp((upperLength * upperLength + solvedDistance * solvedDistance - lowerLength * lowerLength)
                                 / (2.0f * upperLength * solvedDistance), -1.0f, 1.0f);
    const auto hipSine = std::sqrt(std::max(0.0f, 1.0f - hipCosine * hipCosine));
    const auto midPosition = input.rootPosition + direction * (upperLength * hipCosine)
                           + bendDirection * (upperLength * hipSine);

    const auto endPosition = input.rootPosition + direction * solvedDistance;
    const auto requiredDrop = std::max(0.0f, requestedDistance - reachMaximum);
    return { midPosition, endPosition, requestedDistance, solvedDistance, requiredDrop, requiredDrop <= epsilon };
}

Quaternion FootOrientationForGround(Vec3 footUp, Vec3 groundNormal)
{
    return Quaternion::fromTo(footUp, groundNormal);
}

} // namespace ce::animation
