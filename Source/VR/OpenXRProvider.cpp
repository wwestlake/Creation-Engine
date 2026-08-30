#include "VR/OpenXRProvider.h"

#include <cstdio>
#include <iostream>

#if CE_HAS_OPENXR
#include <openxr/openxr.h>
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
    XrInstance instance = XR_NULL_HANDLE;
    const auto createResult = xrCreateInstance(&createInfo, &instance);
    if (createResult != XR_SUCCESS) {
        std::cerr << "OpenXR xrCreateInstance failed: " << static_cast<int>(createResult) << "\n";
        return false;
    }
    XrSystemGetInfo systemInfo{XR_TYPE_SYSTEM_GET_INFO};
    systemInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    XrSystemId system = XR_NULL_SYSTEM_ID;
    const auto systemResult = xrGetSystem(instance, &systemInfo, &system);
    if (systemResult != XR_SUCCESS) {
        std::cerr << "OpenXR xrGetSystem failed: " << static_cast<int>(systemResult) << "\n";
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

void OpenXRProvider::shutdown()
{
#if CE_HAS_OPENXR
    if (instance_ != nullptr) xrDestroyInstance(reinterpret_cast<XrInstance>(instance_));
#endif
    instance_ = nullptr;
    system_ = nullptr;
    state_ = engine::vr::SessionState::unavailable;
}

bool OpenXRProvider::beginFrame(engine::vr::FrameState& frame)
{
    (void)frame;
    return false; // XrSession/swapchains arrive with the renderer integration.
}

bool OpenXRProvider::submitFrame(const engine::vr::FrameState& frame)
{
    (void)frame;
    return false;
}

} // namespace ce::vr
