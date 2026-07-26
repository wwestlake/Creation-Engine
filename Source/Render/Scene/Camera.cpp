#include "Render/Scene/Camera.h"

namespace ce {

void Camera::SetPerspective(float fovYRadians, float aspectRatio, float nearPlane, float farPlane) {
    const float top = nearPlane * std::tan(fovYRadians * 0.5f);
    const float bottom = -top;
    const float right = top * aspectRatio;
    const float left = -right;
    projection_ = juce::Matrix3D<float>::fromFrustum(left, right, bottom, top, nearPlane, farPlane);
}

void Camera::SetLookAt(juce::Vector3D<float> eye, juce::Vector3D<float> target, juce::Vector3D<float> up) {
    eye_ = eye;

    const auto forward = (target - eye).normalised();
    const auto right = (forward ^ up).normalised();
    const auto newUp = right ^ forward;

    view_ = juce::Matrix3D<float>(right.x, newUp.x, -forward.x, 0.0f,
                                   right.y, newUp.y, -forward.y, 0.0f,
                                   right.z, newUp.z, -forward.z, 0.0f,
                                   -(right * eye), -(newUp * eye), (forward * eye), 1.0f);
}

} // namespace ce
