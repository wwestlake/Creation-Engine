#pragma once

#include <atomic>

#include <JuceHeader.h>

namespace ce::input
{
// Windows Raw Input bridge for camera-look capture. It accumulates hardware
// relative deltas without moving the desktop cursor; the camera drains those
// deltas once per frame. Normal JUCE mouse input remains untouched when
// capture is inactive.
class RawMouseInput final : private juce::AsyncUpdater
{
public:
    RawMouseInput() = default;
    ~RawMouseInput() override;

    RawMouseInput(const RawMouseInput&) = delete;
    RawMouseInput& operator=(const RawMouseInput&) = delete;

    // Message-thread entry point, called from the viewport's right-button
    // mouseDown. Returns false only when Windows registration/hooking fails.
    bool beginCapture(juce::Component& owner);

    // Safe from the render thread. The actual OS cursor/capture transition is
    // deferred back to the JUCE message thread by AsyncUpdater.
    void endCapture();

    [[nodiscard]] bool isCapturing() const noexcept { return capturing_.load(std::memory_order_acquire); }
    [[nodiscard]] juce::Point<int> consumeDeltas() noexcept;

private:
    void handleAsyncUpdate() override;

#if JUCE_WINDOWS
    bool attachToWindow(juce::Component& owner);
    void processWindowsMessage(void* rawMessage) noexcept;
    static std::intptr_t __stdcall messageHook(int code, std::uintptr_t wParam, std::intptr_t lParam);
#endif

    std::atomic<bool> capturing_{ false };
    std::atomic<int> accumulatedX_{ 0 };
    std::atomic<int> accumulatedY_{ 0 };
    juce::Component::SafePointer<juce::Component> owner_;

#if JUCE_WINDOWS
    void* windowHandle_ = nullptr;
    void* hookHandle_ = nullptr;
    static std::atomic<RawMouseInput*> activeInstance_;
#endif
};

} // namespace ce::input
