#include "VR/OpenXRProvider.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <vector>

#if CE_HAS_OPENXR
#define XR_USE_PLATFORM_WIN32
#define XR_USE_GRAPHICS_API_OPENGL
#include <windows.h>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#endif

namespace ce::vr {

OpenXRProvider::~OpenXRProvider()
{
    shutdown();
}

bool OpenXRProvider::initialize()
{
    shutdown();
#if CE_HAS_OPENXR
    XrInstanceCreateInfo createInfo{XR_TYPE_INSTANCE_CREATE_INFO};
    createInfo.applicationInfo.apiVersion = XR_CURRENT_API_VERSION;
    std::snprintf(createInfo.applicationInfo.applicationName, XR_MAX_APPLICATION_NAME_SIZE, "Creation Engine");
    std::snprintf(createInfo.applicationInfo.engineName, XR_MAX_ENGINE_NAME_SIZE, "Creation Engine");
    const char* extensions[] = { XR_KHR_OPENGL_ENABLE_EXTENSION_NAME };
    createInfo.enabledExtensionCount = 1;
    createInfo.enabledExtensionNames = extensions;
    XrInstance instance = XR_NULL_HANDLE;
    if (xrCreateInstance(&createInfo, &instance) != XR_SUCCESS) return false;
    XrSystemGetInfo systemInfo{XR_TYPE_SYSTEM_GET_INFO};
    systemInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    XrSystemId system = XR_NULL_SYSTEM_ID;
    if (xrGetSystem(instance, &systemInfo, &system) != XR_SUCCESS) {
        xrDestroyInstance(instance);
        return false;
    }
    instance_ = reinterpret_cast<void*>(instance);
    system_ = reinterpret_cast<void*>(static_cast<std::uintptr_t>(system));
    state_ = engine::vr::SessionState::ready;
    return true;
#else
    return false;
#endif
}

bool OpenXRProvider::initializeOpenGL(void* rawOpenGLContext)
{
#if CE_HAS_OPENXR
    if (instance_ == nullptr || system_ == nullptr || rawOpenGLContext == nullptr || session_ != nullptr)
        return false;

    XrInstance instance = reinterpret_cast<XrInstance>(instance_);
    const auto system = static_cast<XrSystemId>(reinterpret_cast<std::uintptr_t>(system_));
    XrGraphicsRequirementsOpenGLKHR requirements{ XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_KHR };
    PFN_xrGetOpenGLGraphicsRequirementsKHR getRequirements = nullptr;
    if (xrGetInstanceProcAddr(instance, "xrGetOpenGLGraphicsRequirementsKHR",
                              reinterpret_cast<PFN_xrVoidFunction*>(&getRequirements)) != XR_SUCCESS
        || getRequirements(instance, system, &requirements) != XR_SUCCESS) {
        return false;
    }

    XrGraphicsBindingOpenGLWin32KHR binding{ XR_TYPE_GRAPHICS_BINDING_OPENGL_WIN32_KHR };
    binding.hDC = wglGetCurrentDC();
    binding.hGLRC = reinterpret_cast<HGLRC>(rawOpenGLContext);
    if (binding.hDC == nullptr || binding.hGLRC == nullptr)
        return false;

    XrSessionCreateInfo sessionInfo{ XR_TYPE_SESSION_CREATE_INFO };
    sessionInfo.next = &binding;
    sessionInfo.systemId = system;
    XrSession session = XR_NULL_HANDLE;
    if (xrCreateSession(instance, &sessionInfo, &session) != XR_SUCCESS)
        return false;

    XrReferenceSpaceCreateInfo spaceInfo{ XR_TYPE_REFERENCE_SPACE_CREATE_INFO };
    spaceInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_STAGE;
    spaceInfo.poseInReferenceSpace.orientation.w = 1.0f;
    XrSpace space = XR_NULL_HANDLE;
    if (xrCreateReferenceSpace(session, &spaceInfo, &space) != XR_SUCCESS) {
        spaceInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
        if (xrCreateReferenceSpace(session, &spaceInfo, &space) != XR_SUCCESS) {
            xrDestroySession(session);
            return false;
        }
    }

    uint32_t viewCount = 0;
    if (xrEnumerateViewConfigurationViews(instance, system, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
                                          0, &viewCount, nullptr) != XR_SUCCESS || viewCount < 2) {
        xrDestroySpace(space);
        xrDestroySession(session);
        return false;
    }
    std::vector<XrViewConfigurationView> configs(viewCount, XrViewConfigurationView{ XR_TYPE_VIEW_CONFIGURATION_VIEW });
    if (xrEnumerateViewConfigurationViews(instance, system, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
                                          viewCount, &viewCount, configs.data()) != XR_SUCCESS) {
        xrDestroySpace(space);
        xrDestroySession(session);
        return false;
    }

    session_ = reinterpret_cast<void*>(session);
    space_ = reinterpret_cast<void*>(space);
    renderSize_ = { configs[0].recommendedImageRectWidth, configs[0].recommendedImageRectHeight };
    state_ = engine::vr::SessionState::ready;
    return true;
#else
    (void)rawOpenGLContext;
    return false;
#endif
}

void OpenXRProvider::shutdown()
{
#if CE_HAS_OPENXR
    if (session_ != nullptr) {
        auto session = reinterpret_cast<XrSession>(session_);
        if (frameBegun_) {
            XrFrameEndInfo endInfo{ XR_TYPE_FRAME_END_INFO };
            endInfo.displayTime = displayTime_;
            endInfo.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
            xrEndFrame(session, &endInfo);
        }
    }
    if (space_ != nullptr) xrDestroySpace(reinterpret_cast<XrSpace>(space_));
    if (session_ != nullptr) {
        auto session = reinterpret_cast<XrSession>(session_);
        xrDestroySession(session);
    }
    if (instance_ != nullptr) xrDestroyInstance(reinterpret_cast<XrInstance>(instance_));
#endif
    instance_ = nullptr;
    system_ = nullptr;
    session_ = nullptr;
    space_ = nullptr;
    frameBegun_ = false;
    displayTime_ = 0;
    state_ = engine::vr::SessionState::unavailable;
}

bool OpenXRProvider::pollEvents()
{
#if CE_HAS_OPENXR
    if (instance_ == nullptr || session_ == nullptr)
        return false;
    XrEventDataBuffer event{ XR_TYPE_EVENT_DATA_BUFFER };
    while (xrPollEvent(reinterpret_cast<XrInstance>(instance_), &event) == XR_SUCCESS) {
        if (event.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) {
            const auto& changed = reinterpret_cast<const XrEventDataSessionStateChanged&>(event);
            const auto session = reinterpret_cast<XrSession>(session_);
            if (changed.state == XR_SESSION_STATE_READY) {
                XrSessionBeginInfo beginInfo{ XR_TYPE_SESSION_BEGIN_INFO };
                beginInfo.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
                if (xrBeginSession(session, &beginInfo) == XR_SUCCESS)
                    state_ = engine::vr::SessionState::running;
            } else if (changed.state == XR_SESSION_STATE_STOPPING) {
                if (frameBegun_) {
                    XrFrameEndInfo endInfo{ XR_TYPE_FRAME_END_INFO };
                    endInfo.displayTime = displayTime_;
                    endInfo.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
                    xrEndFrame(session, &endInfo);
                    frameBegun_ = false;
                }
                xrEndSession(session);
                state_ = engine::vr::SessionState::stopping;
            } else if (changed.state == XR_SESSION_STATE_EXITING || changed.state == XR_SESSION_STATE_LOSS_PENDING) {
                state_ = engine::vr::SessionState::lost;
            }
        }
        event = XrEventDataBuffer{ XR_TYPE_EVENT_DATA_BUFFER };
    }
    return state_ == engine::vr::SessionState::running;
#else
    return false;
#endif
}

bool OpenXRProvider::beginFrame(engine::vr::FrameState& frame)
{
#if CE_HAS_OPENXR
    if (!pollEvents() || frameBegun_)
        return false;
    XrFrameWaitInfo waitInfo{ XR_TYPE_FRAME_WAIT_INFO };
    XrFrameState xrFrame{ XR_TYPE_FRAME_STATE };
    auto session = reinterpret_cast<XrSession>(session_);
    if (xrWaitFrame(session, &waitInfo, &xrFrame) != XR_SUCCESS)
        return false;
    XrFrameBeginInfo beginInfo{ XR_TYPE_FRAME_BEGIN_INFO };
    if (xrBeginFrame(session, &beginInfo) != XR_SUCCESS)
        return false;

    displayTime_ = xrFrame.predictedDisplayTime;
    frame.frameIndex++;
    frame.predictedDisplayTimeSeconds = static_cast<double>(displayTime_) / 1.0e9;
    XrViewLocateInfo locateInfo{ XR_TYPE_VIEW_LOCATE_INFO };
    locateInfo.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    locateInfo.displayTime = xrFrame.predictedDisplayTime;
    locateInfo.space = reinterpret_cast<XrSpace>(space_);
    XrViewState viewState{ XR_TYPE_VIEW_STATE };
    std::array<XrView, 2> views{ XrView{ XR_TYPE_VIEW }, XrView{ XR_TYPE_VIEW } };
    uint32_t count = 0;
    if (xrLocateViews(session, &locateInfo, &viewState, 2, &count, views.data()) != XR_SUCCESS || count < 2) {
        XrFrameEndInfo endInfo{ XR_TYPE_FRAME_END_INFO };
        endInfo.displayTime = displayTime_;
        endInfo.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
        xrEndFrame(session, &endInfo);
        return false;
    }
    for (int eye = 0; eye < 2; ++eye) {
        auto& target = eye == 0 ? frame.leftEye : frame.rightEye;
        target.pose.position = { views[eye].pose.position.x, views[eye].pose.position.y, views[eye].pose.position.z };
        target.pose.orientation = { views[eye].pose.orientation.x, views[eye].pose.orientation.y,
                                    views[eye].pose.orientation.z, views[eye].pose.orientation.w };
    }
    frameBegun_ = true;
    return true;
#else
    (void)frame;
    return false;
#endif
}

bool OpenXRProvider::submitFrame(const engine::vr::FrameState& frame)
{
#if CE_HAS_OPENXR
    (void)frame;
    if (!frameBegun_ || session_ == nullptr)
        return false;
    XrFrameEndInfo endInfo{ XR_TYPE_FRAME_END_INFO };
    endInfo.displayTime = displayTime_;
    endInfo.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    const bool success = xrEndFrame(reinterpret_cast<XrSession>(session_), &endInfo) == XR_SUCCESS;
    frameBegun_ = false;
    return success;
#else
    (void)frame;
    return false;
#endif
}

} // namespace ce::vr
