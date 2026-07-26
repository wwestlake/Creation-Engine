#include "Render/ViewportComponent.h"

namespace ce {

ViewportComponent::ViewportComponent() {
    openGLContext_.setRenderer(this);
    openGLContext_.attachTo(*this);
    openGLContext_.setContinuousRepainting(true);
}

ViewportComponent::~ViewportComponent() {
    openGLContext_.detach();
}

void ViewportComponent::newOpenGLContextCreated() {
    // Mesh/shader/scene resource creation lands here once the render
    // pipeline exists — this is the hook point, not the full pipeline.
}

void ViewportComponent::renderOpenGL() {
    juce::OpenGLHelpers::clear(juce::Colour(0xff202020));
    // Scene draw calls land here.
}

void ViewportComponent::openGLContextClosing() {
    // Mesh/shader/scene resource teardown lands here, mirroring
    // newOpenGLContextCreated().
}

} // namespace ce
