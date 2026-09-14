#include "Input/RawMouseInput.h"

#if JUCE_WINDOWS
 #include <windows.h>
 #include <hidusage.h>
#endif

namespace ce::input
{
#if JUCE_WINDOWS
std::atomic<RawMouseInput*> RawMouseInput::activeInstance_{ nullptr };
#endif

RawMouseInput::~RawMouseInput()
{
    cancelPendingUpdate();
    endCapture();
#if JUCE_WINDOWS
    if (hookHandle_ != nullptr)
        UnhookWindowsHookEx(static_cast<HHOOK>(hookHandle_));
    RawMouseInput* expected = this;
    activeInstance_.compare_exchange_strong(expected, nullptr, std::memory_order_acq_rel);
#endif
}

bool RawMouseInput::beginCapture(juce::Component& owner)
{
#if JUCE_WINDOWS
    if (!attachToWindow(owner))
        return false;

    owner_ = &owner;
    accumulatedX_.store(0, std::memory_order_release);
    accumulatedY_.store(0, std::memory_order_release);
    capturing_.store(true, std::memory_order_release);
    owner.setMouseCursor(juce::MouseCursor::NoCursor);
    SetCapture(static_cast<HWND>(windowHandle_));
    return true;
#else
    juce::ignoreUnused(owner);
    return false;
#endif
}

void RawMouseInput::endCapture()
{
    if (!capturing_.exchange(false, std::memory_order_acq_rel))
        return;
    accumulatedX_.store(0, std::memory_order_release);
    accumulatedY_.store(0, std::memory_order_release);
    triggerAsyncUpdate();
}

juce::Point<int> RawMouseInput::consumeDeltas() noexcept
{
    return { accumulatedX_.exchange(0, std::memory_order_acq_rel),
             accumulatedY_.exchange(0, std::memory_order_acq_rel) };
}

void RawMouseInput::handleAsyncUpdate()
{
#if JUCE_WINDOWS
    if (windowHandle_ != nullptr && GetCapture() == static_cast<HWND>(windowHandle_))
        ReleaseCapture();
#endif
    if (owner_ != nullptr)
        owner_->setMouseCursor(juce::MouseCursor::NormalCursor);
}

#if JUCE_WINDOWS
bool RawMouseInput::attachToWindow(juce::Component& owner)
{
    auto* peer = owner.getPeer();
    if (peer == nullptr || peer->getNativeHandle() == nullptr)
        return false;

    const auto handle = peer->getNativeHandle();
    if (windowHandle_ == handle && hookHandle_ != nullptr)
        return true;

    if (hookHandle_ != nullptr)
    {
        UnhookWindowsHookEx(static_cast<HHOOK>(hookHandle_));
        hookHandle_ = nullptr;
    }

    windowHandle_ = handle;
    RAWINPUTDEVICE mouse{};
    mouse.usUsagePage = HID_USAGE_PAGE_GENERIC;
    mouse.usUsage = HID_USAGE_GENERIC_MOUSE;
    // Do not use RIDEV_NOLEGACY: regular JUCE mouse events must keep working
    // for the editor outside camera-look capture.
    mouse.dwFlags = RIDEV_INPUTSINK;
    mouse.hwndTarget = static_cast<HWND>(windowHandle_);
    if (RegisterRawInputDevices(&mouse, 1, sizeof(mouse)) == FALSE)
        return false;

    RawMouseInput* expected = nullptr;
    if (!activeInstance_.compare_exchange_strong(expected, this, std::memory_order_acq_rel) && expected != this)
        return false;

    hookHandle_ = SetWindowsHookExW(WH_GETMESSAGE, reinterpret_cast<HOOKPROC>(&RawMouseInput::messageHook),
                                    GetModuleHandleW(nullptr), GetCurrentThreadId());
    if (hookHandle_ == nullptr)
    {
        expected = this;
        activeInstance_.compare_exchange_strong(expected, nullptr, std::memory_order_acq_rel);
        return false;
    }
    return true;
}

std::intptr_t __stdcall RawMouseInput::messageHook(int code, std::uintptr_t wParam, std::intptr_t lParam)
{
    if (code == HC_ACTION && wParam == PM_REMOVE)
    {
        if (auto* instance = activeInstance_.load(std::memory_order_acquire); instance != nullptr)
            instance->processWindowsMessage(reinterpret_cast<void*>(lParam));
    }
    return static_cast<std::intptr_t>(CallNextHookEx(nullptr, code, static_cast<WPARAM>(wParam), static_cast<LPARAM>(lParam)));
}

void RawMouseInput::processWindowsMessage(void* rawMessage) noexcept
{
    if (!capturing_.load(std::memory_order_acquire) || rawMessage == nullptr)
        return;
    const auto& message = *static_cast<const MSG*>(rawMessage);
    if (message.message != WM_INPUT || message.hwnd != static_cast<HWND>(windowHandle_)
        || GetForegroundWindow() != static_cast<HWND>(windowHandle_))
        return;

    RAWINPUT input{};
    UINT inputSize = sizeof(input);
    if (GetRawInputData(reinterpret_cast<HRAWINPUT>(message.lParam), RID_INPUT, &input, &inputSize,
                         sizeof(RAWINPUTHEADER)) == static_cast<UINT>(-1))
        return;
    if (input.header.dwType != RIM_TYPEMOUSE)
        return;

    accumulatedX_.fetch_add(input.data.mouse.lLastX, std::memory_order_relaxed);
    accumulatedY_.fetch_add(input.data.mouse.lLastY, std::memory_order_relaxed);
}
#endif

} // namespace ce::input
