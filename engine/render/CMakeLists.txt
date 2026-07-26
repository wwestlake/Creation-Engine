add_library(engine_render STATIC
    src/window.cpp
)
target_include_directories(engine_render PUBLIC include)
target_link_libraries(engine_render PUBLIC bgfx bx glfw imgui)
target_compile_features(engine_render PUBLIC cxx_std_20)

if(WIN32)
    target_compile_definitions(engine_render PRIVATE GLFW_EXPOSE_NATIVE_WIN32)
endif()
