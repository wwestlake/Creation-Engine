#include "VR/OpenXRProvider.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#if CE_HAS_OPENXR
#include <juce_opengl/juce_opengl.h>
#endif

#if CE_HAS_OPENXR
#ifndef GL_FRAMEBUFFER
#define GL_FRAMEBUFFER 0x8D40
#define GL_COLOR_ATTACHMENT0 0x8CE0
#define GL_TEXTURE_2D 0x0DE1
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#endif
#endif

#if CE_HAS_OPENXR
#define XR_USE_PLATFORM_WIN32
#define XR_USE_GRAPHICS_API_OPENGL
#include <windows.h>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#endif

using namespace juce::gl;

namespace ce::vr {

namespace {

enum InputAction : std::size_t { gripPoseAction, aimPoseAction, thumbstickAction, triggerAction, squeezeAction, primaryAction, menuAction };

void LogVR(const std::string& message)
{
    std::cerr << "[vr] " << message << std::endl;
    // Diagnostics belong with the launched Engine binary, never in a
    // machine-specific user directory. The shared debug executable therefore
    // writes under its configured D: location, while an installed build keeps
    // its diagnostics with its own installation.
    const auto logDirectory = juce::File::getSpecialLocation(juce::File::currentExecutableFile)
                                  .getParentDirectory().getChildFile("logs");
    if (!logDirectory.createDirectory()) return;
    const auto logFile = logDirectory.getChildFile("openxr-diagnostics.log");
    if (auto stream = std::unique_ptr<juce::FileOutputStream>(logFile.createOutputStream(true)))
        stream->writeText("[vr] " + juce::String(message) + "\n", false, false, "UTF-8");
}

#if CE_HAS_OPENXR
void LogXrResult(XrInstance instance, const char* operation, XrResult result)
{
    char resultText[XR_MAX_RESULT_STRING_SIZE]{};
    if (instance != XR_NULL_HANDLE)
        xrResultToString(instance, result, resultText);
    LogVR(std::string(operation) + " -> " + (resultText[0] != '\0' ? resultText : std::to_string(result)));
}
#endif

} // namespace

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
    const auto createResult = xrCreateInstance(&createInfo, &instance);
    if (createResult != XR_SUCCESS) {
        LogXrResult(XR_NULL_HANDLE, "xrCreateInstance", createResult);
        return false;
    }
    XrSystemGetInfo systemInfo{XR_TYPE_SYSTEM_GET_INFO};
    systemInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    XrSystemId system = XR_NULL_SYSTEM_ID;
    const auto systemResult = xrGetSystem(instance, &systemInfo, &system);
    if (systemResult != XR_SUCCESS) {
        LogXrResult(instance, "xrGetSystem", systemResult);
        xrDestroyInstance(instance);
        return false;
    }
    instance_ = reinterpret_cast<void*>(instance);
    system_ = reinterpret_cast<void*>(static_cast<std::uintptr_t>(system));
    state_ = engine::vr::SessionState::ready;
    LogVR("OpenXR instance and HMD system discovered.");
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
        LogVR("Failed to query OpenGL graphics requirements.");
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
    const auto sessionResult = xrCreateSession(instance, &sessionInfo, &session);
    if (sessionResult != XR_SUCCESS) {
        LogXrResult(instance, "xrCreateSession", sessionResult);
        return false;
    }

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

    std::array<XrSwapchainCreateInfo, 2> swapchainInfos{};
    for (int eye = 0; eye < 2; ++eye) {
        uint32_t formatCount = 0;
        if (xrEnumerateSwapchainFormats(session, 0, &formatCount, nullptr) != XR_SUCCESS || formatCount == 0) {
            xrDestroySpace(space);
            xrDestroySession(session);
            return false;
        }
        std::vector<int64_t> formats(formatCount);
        if (xrEnumerateSwapchainFormats(session, formatCount, &formatCount, formats.data()) != XR_SUCCESS) {
            xrDestroySpace(space);
            xrDestroySession(session);
            return false;
        }
        int64_t selectedFormat = formats.front();
        for (const auto format : formats) {
            if (format == 0x8C43 /* GL_SRGB8_ALPHA8 */ || format == 0x8058 /* GL_RGBA8 */) {
                selectedFormat = format;
                break;
            }
        }
        XrSwapchainCreateInfo info{ XR_TYPE_SWAPCHAIN_CREATE_INFO };
        info.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
        info.format = selectedFormat;
        info.sampleCount = configs[eye].recommendedSwapchainSampleCount;
        info.width = configs[eye].recommendedImageRectWidth;
        info.height = configs[eye].recommendedImageRectHeight;
        info.faceCount = 1;
        info.arraySize = 1;
        info.mipCount = 1;
        LogVR("Creating eye " + std::to_string(eye) + " swapchain: " +
              std::to_string(info.width) + "x" + std::to_string(info.height) +
              ", format=" + std::to_string(info.format) + ", samples=" + std::to_string(info.sampleCount));
        XrSwapchain swapchain = XR_NULL_HANDLE;
        const auto swapchainResult = xrCreateSwapchain(session, &info, &swapchain);
        if (swapchainResult != XR_SUCCESS) {
            LogXrResult(instance, "xrCreateSwapchain", swapchainResult);
            xrDestroySpace(space);
            xrDestroySession(session);
            return false;
        }
        uint32_t imageCount = 0;
        if (xrEnumerateSwapchainImages(swapchain, 0, &imageCount, nullptr) != XR_SUCCESS || imageCount == 0) {
            xrDestroySwapchain(swapchain);
            xrDestroySpace(space);
            xrDestroySession(session);
            return false;
        }
        std::vector<XrSwapchainImageOpenGLKHR> images(imageCount,
                                                       XrSwapchainImageOpenGLKHR{ XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_KHR });
        if (xrEnumerateSwapchainImages(swapchain, imageCount, &imageCount,
                                       reinterpret_cast<XrSwapchainImageBaseHeader*>(images.data())) != XR_SUCCESS) {
            xrDestroySwapchain(swapchain);
            xrDestroySpace(space);
            xrDestroySession(session);
            return false;
        }
        swapchains_[eye] = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(swapchain));
        swapchainImages_[eye].reserve(imageCount);
        for (const auto& image : images) swapchainImages_[eye].push_back(image.image);
    }

    session_ = reinterpret_cast<void*>(session);
    space_ = reinterpret_cast<void*>(space);
    renderSize_ = { configs[0].recommendedImageRectWidth, configs[0].recommendedImageRectHeight };
    if (!initializeInput()) {
        LogVR("OpenXR controller actions could not be initialized.");
        return false;
    }
    state_ = engine::vr::SessionState::ready;
    LogVR("OpenXR graphics session initialized successfully.");
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
    for (auto& aimSpace : aimSpaces_) {
        if (aimSpace != nullptr) xrDestroySpace(reinterpret_cast<XrSpace>(aimSpace));
    }
    if (actionSet_ != nullptr) xrDestroyActionSet(reinterpret_cast<XrActionSet>(actionSet_));
    if (space_ != nullptr) xrDestroySpace(reinterpret_cast<XrSpace>(space_));
    if (session_ != nullptr) {
        auto session = reinterpret_cast<XrSession>(session_);
        for (const auto swapchain : swapchains_) {
            if (swapchain != 0) xrDestroySwapchain(reinterpret_cast<XrSwapchain>(static_cast<std::uintptr_t>(swapchain)));
        }
        xrDestroySession(session);
    }
    if (instance_ != nullptr) xrDestroyInstance(reinterpret_cast<XrInstance>(instance_));
#endif
    instance_ = nullptr;
    system_ = nullptr;
    session_ = nullptr;
    space_ = nullptr;
    swapchains_ = {};
    swapchainImages_ = {};
    acquiredImages_ = {};
    framebuffers_ = {};
    imageAcquired_ = {};
    actionSet_ = nullptr;
    actions_ = {};
    aimSpaces_ = {};
    frameBegun_ = false;
    displayTime_ = 0;
    state_ = engine::vr::SessionState::unavailable;
}

bool OpenXRProvider::initializeInput()
{
#if CE_HAS_OPENXR
    const auto instance = reinterpret_cast<XrInstance>(instance_);
    const auto session = reinterpret_cast<XrSession>(session_);
    XrActionSetCreateInfo actionSetInfo{ XR_TYPE_ACTION_SET_CREATE_INFO };
    std::strncpy(actionSetInfo.actionSetName, "creation_engine_editor", XR_MAX_ACTION_SET_NAME_SIZE - 1);
    std::strncpy(actionSetInfo.localizedActionSetName, "Creation Engine Editor", XR_MAX_LOCALIZED_ACTION_SET_NAME_SIZE - 1);
    XrActionSet actionSet = XR_NULL_HANDLE;
    if (xrCreateActionSet(instance, &actionSetInfo, &actionSet) != XR_SUCCESS) return false;
    actionSet_ = reinterpret_cast<void*>(actionSet);

    std::array<XrPath, 2> hands{};
    if (xrStringToPath(instance, "/user/hand/left", &hands[0]) != XR_SUCCESS ||
        xrStringToPath(instance, "/user/hand/right", &hands[1]) != XR_SUCCESS) return false;
    const auto createAction = [&](InputAction index, const char* name, const char* label, XrActionType type) {
        XrActionCreateInfo info{ XR_TYPE_ACTION_CREATE_INFO };
        std::strncpy(info.actionName, name, XR_MAX_ACTION_NAME_SIZE - 1);
        std::strncpy(info.localizedActionName, label, XR_MAX_LOCALIZED_ACTION_NAME_SIZE - 1);
        info.actionType = type;
        info.countSubactionPaths = static_cast<std::uint32_t>(hands.size());
        info.subactionPaths = hands.data();
        XrAction action = XR_NULL_HANDLE;
        if (xrCreateAction(actionSet, &info, &action) != XR_SUCCESS) return false;
        actions_[index] = reinterpret_cast<void*>(action);
        return true;
    };
    if (!createAction(gripPoseAction, "grip_pose", "Grip Pose", XR_ACTION_TYPE_POSE_INPUT) ||
        !createAction(aimPoseAction, "aim_pose", "Aim Pose", XR_ACTION_TYPE_POSE_INPUT) ||
        !createAction(thumbstickAction, "thumbstick", "Thumbstick", XR_ACTION_TYPE_VECTOR2F_INPUT) ||
        !createAction(triggerAction, "trigger", "Trigger", XR_ACTION_TYPE_FLOAT_INPUT) ||
        !createAction(squeezeAction, "squeeze", "Squeeze", XR_ACTION_TYPE_FLOAT_INPUT) ||
        !createAction(primaryAction, "primary", "Primary Button", XR_ACTION_TYPE_BOOLEAN_INPUT) ||
        !createAction(menuAction, "menu", "Menu Button", XR_ACTION_TYPE_BOOLEAN_INPUT)) return false;

    const auto path = [&](const char* value) { XrPath result = XR_NULL_PATH; xrStringToPath(instance, value, &result); return result; };
    std::array<XrActionSuggestedBinding, 14> bindings{{
        { reinterpret_cast<XrAction>(actions_[gripPoseAction]), path("/user/hand/left/input/grip/pose") },
        { reinterpret_cast<XrAction>(actions_[gripPoseAction]), path("/user/hand/right/input/grip/pose") },
        { reinterpret_cast<XrAction>(actions_[aimPoseAction]), path("/user/hand/left/input/aim/pose") },
        { reinterpret_cast<XrAction>(actions_[aimPoseAction]), path("/user/hand/right/input/aim/pose") },
        { reinterpret_cast<XrAction>(actions_[thumbstickAction]), path("/user/hand/left/input/thumbstick") },
        { reinterpret_cast<XrAction>(actions_[thumbstickAction]), path("/user/hand/right/input/thumbstick") },
        { reinterpret_cast<XrAction>(actions_[triggerAction]), path("/user/hand/left/input/trigger/value") },
        { reinterpret_cast<XrAction>(actions_[triggerAction]), path("/user/hand/right/input/trigger/value") },
        { reinterpret_cast<XrAction>(actions_[squeezeAction]), path("/user/hand/left/input/squeeze/value") },
        { reinterpret_cast<XrAction>(actions_[squeezeAction]), path("/user/hand/right/input/squeeze/value") },
        { reinterpret_cast<XrAction>(actions_[primaryAction]), path("/user/hand/left/input/x/click") },
        { reinterpret_cast<XrAction>(actions_[primaryAction]), path("/user/hand/right/input/a/click") },
        { reinterpret_cast<XrAction>(actions_[menuAction]), path("/user/hand/left/input/menu/click") },
        { reinterpret_cast<XrAction>(actions_[menuAction]), path("/user/hand/right/input/b/click") }
    }};
    XrInteractionProfileSuggestedBinding suggested{ XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING };
    suggested.interactionProfile = path("/interaction_profiles/oculus/touch_controller");
    suggested.countSuggestedBindings = static_cast<std::uint32_t>(bindings.size());
    suggested.suggestedBindings = bindings.data();
    if (xrSuggestInteractionProfileBindings(instance, &suggested) != XR_SUCCESS) return false;

    for (std::size_t hand = 0; hand < hands.size(); ++hand) {
        XrActionSpaceCreateInfo spaceInfo{ XR_TYPE_ACTION_SPACE_CREATE_INFO };
        spaceInfo.action = reinterpret_cast<XrAction>(actions_[aimPoseAction]);
        spaceInfo.subactionPath = hands[hand];
        spaceInfo.poseInActionSpace.orientation.w = 1.0f;
        XrSpace aimSpace = XR_NULL_HANDLE;
        if (xrCreateActionSpace(session, &spaceInfo, &aimSpace) != XR_SUCCESS) return false;
        aimSpaces_[hand] = reinterpret_cast<void*>(aimSpace);
    }
    XrSessionActionSetsAttachInfo attach{ XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO };
    attach.countActionSets = 1;
    attach.actionSets = &actionSet;
    return xrAttachSessionActionSets(session, &attach) == XR_SUCCESS;
#else
    return false;
#endif
}

void OpenXRProvider::sampleInput(engine::vr::FrameState& frame)
{
#if CE_HAS_OPENXR
    frame.leftController = {};
    frame.rightController = {};
    if (actionSet_ == nullptr) return;
    const auto instance = reinterpret_cast<XrInstance>(instance_);
    const auto session = reinterpret_cast<XrSession>(session_);
    std::array<XrPath, 2> hands{};
    xrStringToPath(instance, "/user/hand/left", &hands[0]);
    xrStringToPath(instance, "/user/hand/right", &hands[1]);
    const XrActiveActionSet activeSet{ reinterpret_cast<XrActionSet>(actionSet_), XR_NULL_PATH };
    XrActionsSyncInfo syncInfo{ XR_TYPE_ACTIONS_SYNC_INFO };
    syncInfo.countActiveActionSets = 1;
    syncInfo.activeActionSets = &activeSet;
    if (xrSyncActions(session, &syncInfo) != XR_SUCCESS) return;
    const auto getFloat = [&](InputAction action, XrPath hand) {
        XrActionStateGetInfo info{ XR_TYPE_ACTION_STATE_GET_INFO }; info.action = reinterpret_cast<XrAction>(actions_[action]); info.subactionPath = hand;
        XrActionStateFloat state{ XR_TYPE_ACTION_STATE_FLOAT }; xrGetActionStateFloat(session, &info, &state);
        return state.isActive == XR_TRUE ? state.currentState : 0.0f;
    };
    const auto getBoolean = [&](InputAction action, XrPath hand) {
        XrActionStateGetInfo info{ XR_TYPE_ACTION_STATE_GET_INFO }; info.action = reinterpret_cast<XrAction>(actions_[action]); info.subactionPath = hand;
        XrActionStateBoolean state{ XR_TYPE_ACTION_STATE_BOOLEAN }; xrGetActionStateBoolean(session, &info, &state);
        return state.isActive == XR_TRUE && state.currentState == XR_TRUE;
    };
    for (std::size_t hand = 0; hand < hands.size(); ++hand) {
        auto& controller = hand == 0 ? frame.leftController : frame.rightController;
        XrActionStateGetInfo stickInfo{ XR_TYPE_ACTION_STATE_GET_INFO }; stickInfo.action = reinterpret_cast<XrAction>(actions_[thumbstickAction]); stickInfo.subactionPath = hands[hand];
        XrActionStateVector2f stick{ XR_TYPE_ACTION_STATE_VECTOR2F }; xrGetActionStateVector2f(session, &stickInfo, &stick);
        controller.thumbstick = { stick.currentState.x, stick.currentState.y, 0.0f };
        controller.selectPressed = getFloat(triggerAction, hands[hand]) > 0.65f;
        controller.gripPressed = getFloat(squeezeAction, hands[hand]) > 0.65f;
        controller.primaryPressed = getBoolean(primaryAction, hands[hand]);
        controller.menuPressed = getBoolean(menuAction, hands[hand]);
        XrSpaceLocation location{ XR_TYPE_SPACE_LOCATION };
        if (aimSpaces_[hand] != nullptr && xrLocateSpace(reinterpret_cast<XrSpace>(aimSpaces_[hand]), reinterpret_cast<XrSpace>(space_), displayTime_, &location) == XR_SUCCESS &&
            (location.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) != 0 &&
            (location.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) != 0) {
            controller.connected = true;
            controller.pose.position = { location.pose.position.x, location.pose.position.y, location.pose.position.z };
            controller.pose.orientation = { location.pose.orientation.x, location.pose.orientation.y, location.pose.orientation.z, location.pose.orientation.w };
        }
    }
    frame.leftAction = engine::vr::ResolveActions(frame.leftController, frame.leftHand);
    frame.rightAction = engine::vr::ResolveActions(frame.rightController, frame.rightHand);
#else
    (void)frame;
#endif
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
                const auto beginResult = xrBeginSession(session, &beginInfo);
                LogXrResult(reinterpret_cast<XrInstance>(instance_), "xrBeginSession", beginResult);
                if (beginResult == XR_SUCCESS)
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
    const auto waitFrameResult = xrWaitFrame(session, &waitInfo, &xrFrame);
    if (waitFrameResult != XR_SUCCESS) {
        LogXrResult(reinterpret_cast<XrInstance>(instance_), "xrWaitFrame", waitFrameResult);
        return false;
    }
    XrFrameBeginInfo beginInfo{ XR_TYPE_FRAME_BEGIN_INFO };
    const auto beginFrameResult = xrBeginFrame(session, &beginInfo);
    if (beginFrameResult != XR_SUCCESS) {
        LogXrResult(reinterpret_cast<XrInstance>(instance_), "xrBeginFrame", beginFrameResult);
        return false;
    }

    displayTime_ = xrFrame.predictedDisplayTime;
    frame.frameIndex++;
    frame.predictedDisplayTimeSeconds = static_cast<double>(displayTime_) / 1.0e9;
    frame.shouldRender = xrFrame.shouldRender == XR_TRUE;
    frameBegun_ = true;
    if (!frame.shouldRender)
        return true;
    XrViewLocateInfo locateInfo{ XR_TYPE_VIEW_LOCATE_INFO };
    locateInfo.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    locateInfo.displayTime = xrFrame.predictedDisplayTime;
    locateInfo.space = reinterpret_cast<XrSpace>(space_);
    XrViewState viewState{ XR_TYPE_VIEW_STATE };
    std::array<XrView, 2> views{ XrView{ XR_TYPE_VIEW }, XrView{ XR_TYPE_VIEW } };
    uint32_t count = 0;
    const auto locateResult = xrLocateViews(session, &locateInfo, &viewState, 2, &count, views.data());
    if (locateResult != XR_SUCCESS || count < 2) {
        LogXrResult(reinterpret_cast<XrInstance>(instance_), "xrLocateViews", locateResult);
        endFrameWithoutLayers();
        return false;
    }
    sampleInput(frame);
    for (int eye = 0; eye < 2; ++eye) {
        auto& target = eye == 0 ? frame.leftEye : frame.rightEye;
        target.pose.position = { views[eye].pose.position.x, views[eye].pose.position.y, views[eye].pose.position.z };
        target.pose.orientation = { views[eye].pose.orientation.x, views[eye].pose.orientation.y,
                                    views[eye].pose.orientation.z, views[eye].pose.orientation.w };
        target.frustumTangents = { std::tan(views[eye].fov.angleLeft), std::tan(views[eye].fov.angleRight),
                                   std::tan(views[eye].fov.angleDown), std::tan(views[eye].fov.angleUp) };
        target.renderWidth = renderSize_.width;
        target.renderHeight = renderSize_.height;
        fov_[eye * 4 + 0] = views[eye].fov.angleLeft;
        fov_[eye * 4 + 1] = views[eye].fov.angleRight;
        fov_[eye * 4 + 2] = views[eye].fov.angleUp;
        fov_[eye * 4 + 3] = views[eye].fov.angleDown;
        XrSwapchain swapchain = reinterpret_cast<XrSwapchain>(static_cast<std::uintptr_t>(swapchains_[eye]));
        XrSwapchainImageAcquireInfo acquireInfo{ XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO };
        const auto acquireResult = xrAcquireSwapchainImage(swapchain, &acquireInfo, &acquiredImages_[eye]);
        if (acquireResult != XR_SUCCESS) {
            LogXrResult(reinterpret_cast<XrInstance>(instance_), "xrAcquireSwapchainImage", acquireResult);
            endFrameWithoutLayers();
            return false;
        }
        imageAcquired_[eye] = true;
        XrSwapchainImageWaitInfo imageWaitInfo{ XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO };
        imageWaitInfo.timeout = XR_INFINITE_DURATION;
        const auto waitImageResult = xrWaitSwapchainImage(swapchain, &imageWaitInfo);
        if (waitImageResult != XR_SUCCESS) {
            LogXrResult(reinterpret_cast<XrInstance>(instance_), "xrWaitSwapchainImage", waitImageResult);
            endFrameWithoutLayers();
            return false;
        }
        glGenFramebuffers(1, &framebuffers_[eye]);
        glBindFramebuffer(GL_FRAMEBUFFER, framebuffers_[eye]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                               swapchainImages_[eye][acquiredImages_[eye]], 0);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            LogVR("Eye " + std::to_string(eye) + " framebuffer incomplete: status=" +
                  std::to_string(glCheckFramebufferStatus(GL_FRAMEBUFFER)));
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            glDeleteFramebuffers(1, &framebuffers_[eye]);
            framebuffers_[eye] = 0;
            endFrameWithoutLayers();
            return false;
        }
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        target.renderTarget = framebuffers_[eye];
    }
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
    if (!frame.shouldRender)
        return endFrameWithoutLayers();

    std::array<XrCompositionLayerProjectionView, 2> projectionViews{};
    for (int eye = 0; eye < 2; ++eye) {
        projectionViews[eye].type = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW;
        projectionViews[eye].pose = { { frame.leftEye.pose.orientation.x, frame.leftEye.pose.orientation.y,
                                         frame.leftEye.pose.orientation.z, frame.leftEye.pose.orientation.w },
                                       { frame.leftEye.pose.position.x, frame.leftEye.pose.position.y,
                                         frame.leftEye.pose.position.z } };
        if (eye == 1) {
            projectionViews[eye].pose = { { frame.rightEye.pose.orientation.x, frame.rightEye.pose.orientation.y,
                                             frame.rightEye.pose.orientation.z, frame.rightEye.pose.orientation.w },
                                           { frame.rightEye.pose.position.x, frame.rightEye.pose.position.y,
                                             frame.rightEye.pose.position.z } };
        }
        projectionViews[eye].fov = { fov_[eye * 4 + 0], fov_[eye * 4 + 1], fov_[eye * 4 + 2], fov_[eye * 4 + 3] };
        projectionViews[eye].subImage.swapchain = reinterpret_cast<XrSwapchain>(static_cast<std::uintptr_t>(swapchains_[eye]));
        projectionViews[eye].subImage.imageRect = { { 0, 0 },
                                                     { static_cast<int32_t>(frame.leftEye.renderWidth),
                                                       static_cast<int32_t>(frame.leftEye.renderHeight) } };
        if (eye == 1)
            projectionViews[eye].subImage.imageRect.extent = { static_cast<int32_t>(frame.rightEye.renderWidth),
                                                               static_cast<int32_t>(frame.rightEye.renderHeight) };
    }
    XrCompositionLayerProjection layer{ XR_TYPE_COMPOSITION_LAYER_PROJECTION };
    layer.space = reinterpret_cast<XrSpace>(space_);
    layer.viewCount = 2;
    layer.views = projectionViews.data();
    XrFrameEndInfo endInfo{ XR_TYPE_FRAME_END_INFO };
    endInfo.displayTime = displayTime_;
    endInfo.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    const XrCompositionLayerBaseHeader* layers[] = { reinterpret_cast<const XrCompositionLayerBaseHeader*>(&layer) };
    endInfo.layerCount = 1;
    endInfo.layers = layers;
    // OpenXR associates the image used by a layer with the most recently
    // released swapchain image. Release each image before ending the frame;
    // ending first leaves the runtime with an unfinished frame and causes it
    // to discard all following frames.
    for (int eye = 0; eye < 2; ++eye) {
        if (framebuffers_[eye] != 0) {
            glDeleteFramebuffers(1, &framebuffers_[eye]);
            framebuffers_[eye] = 0;
        }
        if (imageAcquired_[eye]) {
            XrSwapchainImageReleaseInfo releaseInfo{ XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO };
            xrReleaseSwapchainImage(reinterpret_cast<XrSwapchain>(static_cast<std::uintptr_t>(swapchains_[eye])), &releaseInfo);
            imageAcquired_[eye] = false;
        }
    }
    const auto endFrameResult = xrEndFrame(reinterpret_cast<XrSession>(session_), &endInfo);
    if (endFrameResult != XR_SUCCESS)
        LogXrResult(reinterpret_cast<XrInstance>(instance_), "xrEndFrame", endFrameResult);
    const bool success = endFrameResult == XR_SUCCESS;
    frameBegun_ = false;
    return success;
#else
    (void)frame;
    return false;
#endif
}

bool OpenXRProvider::endFrameWithoutLayers()
{
#if CE_HAS_OPENXR
    if (!frameBegun_ || session_ == nullptr)
        return false;
    for (int eye = 0; eye < 2; ++eye) {
        if (framebuffers_[eye] != 0) {
            glDeleteFramebuffers(1, &framebuffers_[eye]);
            framebuffers_[eye] = 0;
        }
        if (imageAcquired_[eye]) {
            XrSwapchainImageReleaseInfo releaseInfo{ XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO };
            const auto releaseResult = xrReleaseSwapchainImage(
                reinterpret_cast<XrSwapchain>(static_cast<std::uintptr_t>(swapchains_[eye])), &releaseInfo);
            if (releaseResult != XR_SUCCESS)
                LogXrResult(reinterpret_cast<XrInstance>(instance_), "xrReleaseSwapchainImage", releaseResult);
            imageAcquired_[eye] = false;
        }
    }
    XrFrameEndInfo endInfo{ XR_TYPE_FRAME_END_INFO };
    endInfo.displayTime = displayTime_;
    endInfo.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    const auto result = xrEndFrame(reinterpret_cast<XrSession>(session_), &endInfo);
    if (result != XR_SUCCESS)
        LogXrResult(reinterpret_cast<XrInstance>(instance_), "xrEndFrame(empty)", result);
    frameBegun_ = false;
    return result == XR_SUCCESS;
#else
    return false;
#endif
}

} // namespace ce::vr
