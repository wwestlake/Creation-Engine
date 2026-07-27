# GS1 built the minimal LLVM-JIT skeleton (ce_lang_jit + celc). GS2 adds
# ce_lang_frontend: the LALR(1) grammar (Bison/Flex-generated), AST, and
# diagnostics -- deliberately ZERO llvm/*.h includes, so it can be
# reused later (e.g. by nodegen) without dragging LLVM along. ce_lang_jit
# remains the ONLY target that ever includes an llvm/*.h header (see
# include/lang/jit/runtime.h's own comment).
include("${CMAKE_SOURCE_DIR}/cmake/CelGrammar.cmake")

ce_add_cel_grammar(CE_CEL_GRAMMAR_SOURCES)

add_library(ce_lang_frontend STATIC
    include/lang/source_location.h
    include/lang/diagnostics.h
    include/lang/type.h
    include/lang/ast.h
    include/lang/ast_printer.h
    include/lang/lexer.h
    include/lang/compiler.h
    include/lang/sema.h
    include/lang/intrinsics.def
    src/source_location.cpp
    src/diagnostics.cpp
    src/type.cpp
    src/ast.cpp
    src/ast_printer.cpp
    src/compiler.cpp
    src/sema.cpp
    ${CE_CEL_GRAMMAR_SOURCES}
)

target_include_directories(ce_lang_frontend
    PUBLIC include
    PRIVATE "${CE_CEL_GRAMMAR_GEN_DIR}" "${CE_FLEXLEXER_INCLUDE_DIR}"
)
target_compile_features(ce_lang_frontend PUBLIC cxx_std_20)
# Bison/Flex-generated code trips a handful of benign MSVC warnings
# (unreferenced formal parameters, signed/unsigned mismatches in the
# generated scanner/parser tables) that aren't worth suppressing
# line-by-line in code this project doesn't hand-author -- scoped to
# just the generated sources, not the whole target, so hand-written
# Language/ code still gets full warnings.
set_source_files_properties(${CE_CEL_GRAMMAR_SOURCES} PROPERTIES COMPILE_OPTIONS "/W0")

find_package(LLVM CONFIG REQUIRED)
message(STATUS "Language: found LLVM ${LLVM_PACKAGE_VERSION} at ${LLVM_DIR}")

llvm_map_components_to_libnames(CE_LLVM_LIBS core orcjit native)

add_library(ce_lang_jit STATIC
    include/lang/jit/runtime.h
    src/jit/runtime.cpp
    include/lang/jit/script_runtime.h
    src/jit/script_runtime.cpp
    src/jit/optimizer.h
    src/jit/optimizer.cpp
    src/jit/module_builder.h
    src/jit/module_builder.cpp
    src/jit/intrinsic_trampolines.h
    src/jit/intrinsic_trampolines.cpp
    src/jit/abi_registration.h
    src/jit/abi_registration.cpp
)

target_include_directories(ce_lang_jit
    PUBLIC include
    PRIVATE ${LLVM_INCLUDE_DIRS}
)
target_compile_definitions(ce_lang_jit PRIVATE ${LLVM_DEFINITIONS} NOMINMAX WIN32_LEAN_AND_MEAN)
# PUBLIC: runtime.h (a public header of this target) includes lang/ast.h
# since GS4's CompileAndRun takes a Program& directly -- ce_lang_frontend
# stays LLVM-free either way (see its own comment), this just makes the
# AST types visible to anything that includes runtime.h.
#
# engine_core (GS5): intrinsic_trampolines.cpp/runtime.cpp's
# RunWorldProgram construct a real ce::engine::World/ScriptContext --
# EngineCore has no JUCE/LLVM dependency of its own, so linking it here
# doesn't threaten the LLVM/JUCE header-firewall this module exists to
# preserve. engine_core's own add_subdirectory() call is later in the
# root CMakeLists.txt than Language's, but CMake resolves
# target_link_libraries against the full target graph at generate time,
# not in add_subdirectory() call order -- the same precedent EngineCore
# itself already relies on for node_system.
target_link_libraries(ce_lang_jit PUBLIC ce_lang_frontend engine_core ${CE_LLVM_LIBS})
target_compile_features(ce_lang_jit PUBLIC cxx_std_20)

# Plain add_executable, deliberately NOT juce_add_console_app: this
# guarantees at least one full build path where LLVM and JUCE headers
# never meet in the same translation unit, which is cheap insurance
# against the header-collision risk vcpkg LLVM + JUCE + windows.h
# carries (min/max macros, _ITERATOR_DEBUG_LEVEL mismatches).
add_executable(celc tools/celc/main.cpp)
target_link_libraries(celc PRIVATE ce_lang_frontend ce_lang_jit)

add_test(NAME gs1_llvm_selftest COMMAND celc --selftest-jit)

# GS2: every Language/tests/parse/*.cel round-tripped through
# `celc --dump-ast` and diffed against its committed .expected fixture.
# One CTest per fixture (not one big loop test) so a single regression
# names the exact failing program instead of an opaque "some test
# failed."
file(GLOB CE_CEL_PARSE_TESTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/parse/*.cel")
foreach(test_file ${CE_CEL_PARSE_TESTS})
    get_filename_component(test_name "${test_file}" NAME_WE)
    get_filename_component(test_dir "${test_file}" DIRECTORY)
    add_test(
        NAME "gs2_parse_${test_name}"
        COMMAND ${CMAKE_COMMAND}
                -DCELC_EXE=$<TARGET_FILE:celc>
                -DTEST_CEL=${test_file}
                -DEXPECTED_FILE=${test_dir}/${test_name}.expected
                -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_dump_ast_test.cmake"
    )
endforeach()

# Language/tests/diag/*.cel are deliberately-broken programs paired with
# a *.expect_code sidecar naming the CELxxxx diagnostic they must
# produce -- proves the diagnostic engine reports the *right* problem,
# not just that parsing failed for some reason or other.
file(GLOB CE_CEL_DIAG_TESTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/diag/*.cel")
foreach(test_file ${CE_CEL_DIAG_TESTS})
    get_filename_component(test_name "${test_file}" NAME_WE)
    get_filename_component(test_dir "${test_file}" DIRECTORY)
    add_test(
        NAME "gs2_diag_${test_name}"
        COMMAND ${CMAKE_COMMAND}
                -DCELC_EXE=$<TARGET_FILE:celc>
                -DTEST_CEL=${test_file}
                -DEXPECT_CODE_FILE=${test_dir}/${test_name}.expect_code
                -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_diag_test.cmake"
    )
endforeach()

# GS3: Language/tests/sema/ok/*.cel must type-check cleanly
# (`celc --check` exits 0); Language/tests/sema/fail/*.cel are paired
# with *.expect_code the same way tests/diag/ is, but exercised through
# --check (semantic errors) rather than --dump-ast (syntax errors).
file(GLOB CE_CEL_SEMA_OK_TESTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/sema/ok/*.cel")
foreach(test_file ${CE_CEL_SEMA_OK_TESTS})
    get_filename_component(test_name "${test_file}" NAME_WE)
    add_test(
        NAME "gs3_sema_ok_${test_name}"
        COMMAND ${CMAKE_COMMAND}
                -DCELC_EXE=$<TARGET_FILE:celc>
                -DTEST_CEL=${test_file}
                -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_check_ok_test.cmake"
    )
endforeach()

file(GLOB CE_CEL_SEMA_FAIL_TESTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/sema/fail/*.cel")
foreach(test_file ${CE_CEL_SEMA_FAIL_TESTS})
    get_filename_component(test_name "${test_file}" NAME_WE)
    get_filename_component(test_dir "${test_file}" DIRECTORY)
    add_test(
        NAME "gs3_sema_fail_${test_name}"
        COMMAND ${CMAKE_COMMAND}
                -DCELC_EXE=$<TARGET_FILE:celc>
                -DTEST_CEL=${test_file}
                -DEXPECT_CODE_FILE=${test_dir}/${test_name}.expect_code
                -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_sema_diag_test.cmake"
    )
endforeach()

# GS4: Language/tests/exec/*.cel are real, hand-computed programs run
# through `celc --run` at both -O0 and -O2 -- see run_exec_test.cmake
# for why both, not just one.
file(GLOB CE_CEL_EXEC_TESTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/exec/*.cel")
foreach(test_file ${CE_CEL_EXEC_TESTS})
    get_filename_component(test_name "${test_file}" NAME_WE)
    get_filename_component(test_dir "${test_file}" DIRECTORY)
    add_test(
        NAME "gs4_exec_${test_name}"
        COMMAND ${CMAKE_COMMAND}
                -DCELC_EXE=$<TARGET_FILE:celc>
                -DTEST_CEL=${test_file}
                -DEXPECTED_FILE=${test_dir}/${test_name}.expected
                -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_exec_test.cmake"
    )
endforeach()

# GS5: Language/tests/world/*.cel exercise the real host-ABI intrinsics
# (World/Entity/Transform/Debug) against a genuine, internally-
# constructed ce::engine::World via `celc --run-world` -- unlike
# tests/exec/, each fixture needs its own entry point/tick count/dt (see
# run_world_test.cmake's own comment), so these are explicit add_test()
# calls rather than a glob+loop.
add_test(
    NAME gs5_world_spawn_grid
    COMMAND ${CMAKE_COMMAND}
            -DCELC_EXE=$<TARGET_FILE:celc>
            -DTEST_CEL=${CMAKE_CURRENT_SOURCE_DIR}/tests/world/spawn_grid.cel
            -DEXPECTED_FILE=${CMAKE_CURRENT_SOURCE_DIR}/tests/world/spawn_grid.expected
            -DENTRY=main
            -DTICKS=1
            -DDT=0.016666667
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_world_test.cmake"
)
add_test(
    NAME gs5_world_orbit_steps
    COMMAND ${CMAKE_COMMAND}
            -DCELC_EXE=$<TARGET_FILE:celc>
            -DTEST_CEL=${CMAKE_CURRENT_SOURCE_DIR}/tests/world/orbit_steps.cel
            -DEXPECTED_FILE=${CMAKE_CURRENT_SOURCE_DIR}/tests/world/orbit_steps.expected
            -DENTRY=tick
            -DTICKS=300
            -DDT=0.016666667
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_world_test.cmake"
)

# GS5: a genuinely infinite loop must be stopped by the runaway-script
# watchdog (CEL9001), not hang the test suite -- see
# tests/watchdog/infinite_loop.cel and run_watchdog_test.cmake.
add_test(
    NAME gs5_watchdog_infinite_loop
    COMMAND ${CMAKE_COMMAND}
            -DCELC_EXE=$<TARGET_FILE:celc>
            -DTEST_CEL=${CMAKE_CURRENT_SOURCE_DIR}/tests/watchdog/infinite_loop.cel
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_watchdog_test.cmake"
)

# GS6: `celc --run-simulation` exercises the REAL production pipeline --
# ce::engine::Simulation::Step + ScriptComponent + CelScriptRuntime --
# unlike GS5's tests/world/ (which drive Runtime::RunWorldProgram
# directly). See run_simulation_test.cmake for the two-line output
# format; the cross-tool (celc vs CreationEngineServer) parity test
# lives in the root CMakeLists.txt, since it needs both executables.
add_test(
    NAME gs6_simulation_orbit
    COMMAND ${CMAKE_COMMAND}
            -DCELC_EXE=$<TARGET_FILE:celc>
            -DTEST_CEL=${CMAKE_CURRENT_SOURCE_DIR}/tests/simulation/orbit.cel
            -DEXPECTED_FILE=${CMAKE_CURRENT_SOURCE_DIR}/tests/simulation/orbit.expected
            -DTICKS=300
            -DDT=0.016666667
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_simulation_test.cmake"
)

# GS7: the hard "<1s compile time" requirement, measured (not assumed) --
# see run_compile_perf_test.cmake.
add_test(
    NAME gs7_compile_perf_orbit
    COMMAND ${CMAKE_COMMAND}
            -DCELC_EXE=$<TARGET_FILE:celc>
            -DTEST_CEL=${CMAKE_CURRENT_SOURCE_DIR}/tests/simulation/orbit.cel
            -DMAX_MS=1000
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_compile_perf_test.cmake"
)
