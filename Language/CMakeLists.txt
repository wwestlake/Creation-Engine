# GS1: minimal scripting-language module skeleton -- proves LLVM (via
# vcpkg) links and JITs in this exact toolchain before any grammar/AST/
# codegen work is built on top of it. From GS2 onward this module also
# gains ce_lang_frontend (lexer/parser/AST/sema, zero LLVM headers) and
# ce_lang_nodegen (graph-to-source, zero LLVM headers) -- ce_lang_jit is
# the only target here that ever compiles an llvm/*.h include, and
# Language/src/jit/*.cpp is the only place those includes are allowed to
# appear (see include/lang/jit/runtime.h's own header comment).
find_package(LLVM CONFIG REQUIRED)
message(STATUS "Language: found LLVM ${LLVM_PACKAGE_VERSION} at ${LLVM_DIR}")

llvm_map_components_to_libnames(CE_LLVM_LIBS core orcjit native)

add_library(ce_lang_jit STATIC
    include/lang/jit/runtime.h
    src/jit/runtime.cpp
)

target_include_directories(ce_lang_jit
    PUBLIC include
    PRIVATE ${LLVM_INCLUDE_DIRS}
)
target_compile_definitions(ce_lang_jit PRIVATE ${LLVM_DEFINITIONS} NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(ce_lang_jit PUBLIC ${CE_LLVM_LIBS})
target_compile_features(ce_lang_jit PUBLIC cxx_std_20)

# Plain add_executable, deliberately NOT juce_add_console_app: this
# guarantees at least one full build path where LLVM and JUCE headers
# never meet in the same translation unit, which is cheap insurance
# against the header-collision risk vcpkg LLVM + JUCE + windows.h
# carries (min/max macros, _ITERATOR_DEBUG_LEVEL mismatches).
add_executable(celc tools/celc/main.cpp)
target_link_libraries(celc PRIVATE ce_lang_jit)

add_test(NAME gs1_llvm_selftest COMMAND celc --selftest-jit)
