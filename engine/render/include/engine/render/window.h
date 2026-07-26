#pragma once

#include <cstdint>
#include <string>

struct GLFWwindow;

namespace ce::engine::render {

// Owns the OS window and the bgfx renderer bound to it. This is the
// surface both the "editor" and the "game" presentation modes draw
// through — per spec section 1, the editor and the runtime are the same
// executable in different modes, not two separate programs.
//
// Tooling UI (inspectors, node graph editors) is drawn as an ImGui overlay
// on top of this same viewport (section 3.4) — submitting ImGui draw data
// through bgfx is the next slice of work on top of this scaffold.
class Window {
public:
    Window(int width, int height, const std::string& title);
    ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    bool ShouldClose() const;
    void PollEvents();

    // Clears the backbuffer and advances the bgfx frame. Returns the
    // frame number bgfx assigned this submission.
    std::uint32_t BeginFrame();
    void EndFrame();

    int Width() const { return width_; }
    int Height() const { return height_; }

private:
    GLFWwindow* glfwWindow_ = nullptr;
    int width_;
    int height_;
};

} // namespace ce::engine::render
