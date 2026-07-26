#pragma once

#include <JuceHeader.h>

namespace ce {

// Minimal view/projection camera. No component/entity hookup yet — this
// is the render layer's own camera until the editor needs to bind it to
// something the node graphs or ECS can drive.
class Camera final {
public:
    void SetPerspective(float fovYRadians, float aspectRatio, float nearPlane, float farPlane);
    void SetLookAt(juce::Vector3D<float> eye, juce::Vector3D<float> target,
                    juce::Vector3D<float> up = juce::Vector3D<float>::yAxis());

    const juce::Matrix3D<float>& ViewMatrix() const { return view_; }
    const juce::Matrix3D<float>& ProjectionMatrix() const { return projection_; }
    juce::Vector3D<float> Position() const { return eye_; }

private:
    juce::Matrix3D<float> view_;
    juce::Matrix3D<float> projection_;
    juce::Vector3D<float> eye_;
};

} // namespace ce
