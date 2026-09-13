#include "Animation/ProceduralLocomotion.h"

#include <cmath>
#include <cstdlib>
#include <iostream>

using namespace ce::animation;

namespace
{
constexpr float tolerance = 1.0e-3f;

[[noreturn]] void fail(const char* message)
{
    std::cerr << "ProceduralLocomotionSmoke failure: " << message << '\n';
    std::exit(1);
}

void require(bool value, const char* message)
{
    if (!value)
        fail(message);
}

bool approximately(float left, float right, float threshold = tolerance)
{
    return std::abs(left - right) <= threshold;
}
}

int main()
{
    const TwoBoneIKInput neutralLeg{ { 0.0f, 1.0f, 0.0f }, { 0.0f, 0.0f, 0.20f },
                                     { 0.0f, 0.0f, 1.0f }, { 0.0f, 1.0f, 0.0f }, 0.55f, 0.55f };
    const auto neutral = SolveTwoBoneIK(neutralLeg);
    require(neutral.reachableWithoutPelvisAdjustment, "reachable leg requested pelvis adjustment");
    require(approximately((neutral.midPosition - neutralLeg.rootPosition).length(), neutralLeg.upperLength),
            "upper leg length changed");
    require(approximately((neutral.endPosition - neutral.midPosition).length(), neutralLeg.lowerLength),
            "lower leg length changed");
    require(neutral.midPosition.z > neutralLeg.rootPosition.z, "pole direction did not choose the forward knee side");

    auto unreachableLeg = neutralLeg;
    unreachableLeg.targetPosition = { 0.0f, -1.0f, 0.0f };
    const auto unreachable = SolveTwoBoneIK(unreachableLeg);
    require(!unreachable.reachableWithoutPelvisAdjustment, "unreachable leg was accepted");
    require(unreachable.requiredPelvisDrop > 0.5f, "unreachable leg did not report correction");
    require(approximately((unreachable.midPosition - unreachableLeg.rootPosition).length(), unreachableLeg.upperLength),
            "unreachable upper leg stretched");
    require(approximately((unreachable.endPosition - unreachable.midPosition).length(), unreachableLeg.lowerLength),
            "unreachable lower leg stretched");

    const auto arcStart = Vec3{ 0.0f, 0.0f, 0.0f };
    const auto arcEnd = Vec3{ 1.0f, 0.0f, 0.0f };
    require(EvaluateSwingArc(arcStart, arcEnd, { 0.0f, 1.0f, 0.0f }, 0.0f, 0.15f).length() < tolerance,
            "swing arc does not start at start target");
    require((EvaluateSwingArc(arcStart, arcEnd, { 0.0f, 1.0f, 0.0f }, 0.5f, 0.15f)).y > 0.14f,
            "swing arc lacks clearance");
    require((EvaluateSwingArc(arcStart, arcEnd, { 0.0f, 1.0f, 0.0f }, 1.0f, 0.15f) - arcEnd).length() < tolerance,
            "swing arc does not finish at end target");

    const Transform support{ { 3.0f, 0.0f, -2.0f }, Quaternion::fromAxisAngle({ 0.0f, 1.0f, 0.0f }, 0.5f) };
    const Vec3 plantedWorld{ 3.5f, 0.2f, -1.8f };
    const auto anchor = MakeContactAnchor(42, support, plantedWorld, { 0.0f, 1.0f, 0.0f });
    require((ResolveContactPosition(anchor, support) - plantedWorld).length() < tolerance,
            "contact anchor cannot reconstruct planted world position");

    const auto footRotation = FootOrientationForGround({ 0.0f, 1.0f, 0.0f }, { 0.0f, 0.7071067f, 0.7071067f });
    require(Dot(footRotation.rotate({ 0.0f, 1.0f, 0.0f }), { 0.0f, 0.7071067f, 0.7071067f }) > 0.999f,
            "foot alignment does not map up to the ground normal");

    const FootstepPlanInput stepInput{ { 0.0f, 1.0f, 0.0f }, { 0.0f, 0.0f, -1.0f }, { 0.0f, 0.0f, -1.0f },
                                       { 0.0f, 0.0f, -1.0f }, { 0.0f, 1.0f, 0.0f }, 9.81f, 1.0f, 0.4f,
                                       0.20f, 0.18f, GaitSide::left };
    const auto step = PlanFootstep(stepInput);
    require(step.naturalFrequency > 3.0f, "invalid LIPM natural frequency");
    require(step.predictedLanding.x < -0.05f && step.predictedLanding.z < -0.4f,
            "capture step has no forward prediction or lateral stance");
    require(approximately(step.predictedLanding.y, 1.0f),
            "capture step changed centre-of-mass elevation");

    std::cout << "ProceduralLocomotionSmoke passed.\n";
    return 0;
}
