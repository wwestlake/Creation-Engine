#include "engine/render/window.h"

#include <stdexcept>

#include <GLFW/glfw3.h>
#if defined(_WIN32)
#include <GLFW/glfw3native.h>
#endif

#include <bgfx/bgfx.h>
#include <bgfx/platform.h>

namespace ce::engine::render {

namespace {
constexpr bgfx::ViewId kMainView = 0;
}

Window::Window(int width, int height, const std::string& title) : width_(width), height_(height) {
    if (!glfwInit()) {
        throw std::runtime_error("failed to initialize GLFW");
    }

    // bgfx owns the graphics device; GLFW just gives us the native window
    // handle to bind it to.
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindow_ = glfwCreateWindow(width, height, title.c_str(), nullptr, nullptr);
    if (!glfwWindow_) {
        glfwTerminate();
        throw std::runtime_error("failed to create window");
    }

    bgfx::PlatformData platformData{};
#if defined(_WIN32)
    platformData.nwh = glfwGetWin32Window(glfwWindow_);
#endif
    bgfx::Init init;
    init.type = bgfx::RendererType::Count; // auto-select best backend for this platform
    init.platformData = platformData;
    init.resolution.width = static_cast<std::uint32_t>(width);
    init.resolution.height = static_cast<std::uint32_t>(height);
    init.resolution.reset = BGFX_RESET_VSYNC;
    if (!bgfx::init(init)) {
        throw std::runtime_error("failed to initialize bgfx");
    }

    bgfx::setViewClear(kMainView, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0x202020ff, 1.0f, 0);
    bgfx::setViewRect(kMainView, 0, 0, bgfx::BackbufferRatio::Equal);
}

Window::~Window() {
    bgfx::shutdown();
    if (glfwWindow_) {
        glfwDestroyWindow(glfwWindow_);
    }
    glfwTerminate();
}

bool Window::ShouldClose() const {
    return glfwWindowShouldClose(glfwWindow_) != 0;
}

void Window::PollEvents() {
    glfwPollEvents();

    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(glfwWindow_, &width, &height);
    if (width != width_ || height != height_) {
        width_ = width;
        height_ = height;
        bgfx::reset(static_cast<std::uint32_t>(width_), static_cast<std::uint32_t>(height_), BGFX_RESET_VSYNC);
        bgfx::setViewRect(kMainView, 0, 0, bgfx::BackbufferRatio::Equal);
    }
}

std::uint32_t Window::BeginFrame() {
    bgfx::touch(kMainView);
    return bgfx::frame();
}

void Window::EndFrame() {
    // Reserved for post-frame bookkeeping (e.g. ImGui draw submission)
    // once the tooling overlay is wired in.
}

} // namespace ce::engine::render
