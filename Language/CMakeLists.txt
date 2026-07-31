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

# GS9: graph -> CEL source text generation. Deliberately linked against
# ce_lang_frontend + node_system only -- NOT ce_lang_jit/LLVM (see
# include/lang/nodegen/graph_to_source.h's own comment) -- so this
# target can never accidentally depend on the JIT to produce source
# text; only celc's --run-graph combines nodegen's output with
# ce_lang_jit to actually execute it.
add_library(ce_lang_nodegen STATIC
    include/lang/nodegen/node_catalog.h
    src/nodegen/node_catalog.cpp
    include/lang/nodegen/graph_to_source.h
    src/nodegen/graph_to_source.cpp
)
target_include_directories(ce_lang_nodegen PUBLIC include)
target_link_libraries(ce_lang_nodegen PUBLIC ce_lang_frontend node_system)
target_compile_features(ce_lang_nodegen PUBLIC cxx_std_20)

# Plain add_executable, deliberately NOT juce_add_console_app: this
# guarantees at least one full build path where LLVM and JUCE headers
# never meet in the same translation unit, which is cheap insurance
# against the header-collision risk vcpkg LLVM + JUCE + windows.h
# carries (min/max macros, _ITERATOR_DEBUG_LEVEL mismatches).
add_executable(celc
    tools/celc/main.cpp
    tools/celc/selftest_graph.h
    tools/celc/selftest_graph.cpp
    tools/celc/nodegen_fixtures.h
    tools/celc/nodegen_fixtures.cpp
)
# node_system: PRIVATE -- celc's own GS8 test code (selftest_graph.cpp)
# and --check-graph use NodeSystem directly; ce_lang_jit already brings
# it in transitively via engine_core (both PUBLIC links), so this isn't
# strictly required for the include path to resolve, but it's the
# correct explicit dependency for what celc's OWN sources use, not just
# what happens to ride along. ce_lang_nodegen (GS9): --graph-to-source/
# --run-graph.
target_link_libraries(celc PRIVATE ce_lang_frontend ce_lang_jit ce_lang_nodegen node_system)

add_test(NAME gs1_llvm_selftest COMMAND celc --selftest-jit)
add_test(NAME gs8_selftest_graph COMMAND celc --selftest-graph)

# GS8: a committed .celg fixture must load and re-save unchanged -- see
# run_celg_stability_test.cmake's own comment.
add_test(
    NAME gs8_celg_stability_example
    COMMAND ${CMAKE_COMMAND}
            -DCELC_EXE=$<TARGET_FILE:celc>
            -DTEST_CELG=${CMAKE_CURRENT_SOURCE_DIR}/tests/graph/example.celg
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_celg_stability_test.cmake"
)

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
    NAME gs_world_color_roundtrip
    COMMAND ${CMAKE_COMMAND}
            -DCELC_EXE=$<TARGET_FILE:celc>
            -DTEST_CEL=${CMAKE_CURRENT_SOURCE_DIR}/tests/world/color_roundtrip.cel
            -DEXPECTED_FILE=${CMAKE_CURRENT_SOURCE_DIR}/tests/world/color_roundtrip.expected
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

# GS-Interop: cross-app intrinsic capability gating -- the same script
# (calls a World-domain intrinsic) must be REJECTED under a narrower
# capability profile and must still SUCCEED under the real default
# (IntrinsicDomainSet::All()) -- see docs/CROSS_APP_LANGUAGE_DOMAINS.md
# and run_domain_diag_test.cmake/run_domain_ok_test.cmake's own
# comments for why both halves of this pair matter.
add_test(
    NAME gs_interop_domain_blocked
    COMMAND ${CMAKE_COMMAND}
            -DCELC_EXE=$<TARGET_FILE:celc>
            -DTEST_CEL=${CMAKE_CURRENT_SOURCE_DIR}/tests/domain/blocked_world_call.cel
            -DEXPECT_CODE_FILE=${CMAKE_CURRENT_SOURCE_DIR}/tests/domain/blocked_world_call.expect_code
            -DDOMAINS=core
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_domain_diag_test.cmake"
)
add_test(
    NAME gs_interop_domain_allowed
    COMMAND ${CMAKE_COMMAND}
            -DCELC_EXE=$<TARGET_FILE:celc>
            -DTEST_CEL=${CMAKE_CURRENT_SOURCE_DIR}/tests/domain/blocked_world_call.cel
            -DDOMAINS=core,world
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_domain_ok_test.cmake"
)

# GS9: the flagship verification for graph -> CEL source generation --
# Language/tests/nodegen/bounce.celg (built programmatically, see
# Language/tools/celc/nodegen_fixtures.cpp) and bounce.cel (hand-written)
# describe the SAME behavior; run through --run-graph/--run-simulation
# respectively for 300 ticks, their output must be byte-identical. See
# run_nodegen_parity_test.cmake's own comment for why this is the
# single strongest proof in this milestone.
add_test(
    NAME gs9_nodegen_parity_bounce
    COMMAND ${CMAKE_COMMAND}
            -DCELC_EXE=$<TARGET_FILE:celc>
            -DTEST_CELG=${CMAKE_CURRENT_SOURCE_DIR}/tests/nodegen/bounce.celg
            -DTEST_CEL=${CMAKE_CURRENT_SOURCE_DIR}/tests/nodegen/bounce.cel
            -DEXPECTED_FILE=${CMAKE_CURRENT_SOURCE_DIR}/tests/nodegen/bounce.expected
            -DTICKS=300
            -DDT=0.016666667
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_nodegen_parity_test.cmake"
)

# GS9: a text-level determinism/coverage check for the catalog types
# gs9_nodegen_parity_bounce doesn't reach -- see
# run_graph_to_source_test.cmake's own comment.
add_test(
    NAME gs9_graph_to_source_operator_coverage
    COMMAND ${CMAKE_COMMAND}
            -DCELC_EXE=$<TARGET_FILE:celc>
            -DTEST_CELG=${CMAKE_CURRENT_SOURCE_DIR}/tests/nodegen/operator_coverage.celg
            -DEXPECTED_FILE=${CMAKE_CURRENT_SOURCE_DIR}/tests/nodegen/operator_coverage.expected
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_graph_to_source_test.cmake"
)

# GS9: a graph with no OnStart/OnTick entry node must be REJECTED by
# GenerateSource with a clear error, not silently produce empty/partial
# source -- see run_graph_to_source_error_test.cmake's own comment.
add_test(
    NAME gs9_graph_to_source_no_entry
    COMMAND ${CMAKE_COMMAND}
            -DCELC_EXE=$<TARGET_FILE:celc>
            -DTEST_CELG=${CMAKE_CURRENT_SOURCE_DIR}/tests/nodegen/no_entry.celg
            -DEXPECT_SUBSTRING=no OnStart, OnTick, or SubgraphEntry
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_graph_to_source_error_test.cmake"
)

# GS11: subgraph.celg (SubgraphEntry("Helper") called twice via two
# CallSubgraph nodes, built via BuildSubgraphGraph) and subgraph.cel
# (hand-written) describe the identical behavior -- run for real via
# --run-graph/--run-simulation, checked against a real final position
# (2,0,0) that's only correct if Helper genuinely ran exactly twice.
add_test(
    NAME gs11_subgraph_parity
    COMMAND ${CMAKE_COMMAND}
            -DCELC_EXE=$<TARGET_FILE:celc>
            -DTEST_CELG=${CMAKE_CURRENT_SOURCE_DIR}/tests/nodegen/subgraph.celg
            -DTEST_CEL=${CMAKE_CURRENT_SOURCE_DIR}/tests/nodegen/subgraph.cel
            -DEXPECTED_FILE=${CMAKE_CURRENT_SOURCE_DIR}/tests/nodegen/subgraph.expected
            -DTICKS=1
            -DDT=0.016666667
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_nodegen_parity_test.cmake"
)

# GS11: the flagship "a subgraph called twice emits one func + two call
# sites" claim, checked as generated TEXT (the parity test above checks
# it as real behavior).
add_test(
    NAME gs11_subgraph_source_shape
    COMMAND ${CMAKE_COMMAND}
            -DCELC_EXE=$<TARGET_FILE:celc>
            -DTEST_CELG=${CMAKE_CURRENT_SOURCE_DIR}/tests/nodegen/subgraph.celg
            -DEXPECTED_FILE=${CMAKE_CURRENT_SOURCE_DIR}/tests/nodegen/subgraph_source.expected
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_graph_to_source_test.cmake"
)

# GS11: a CallFunction node naming an undefined function generates
# cleanly but fails sema -- CheckGeneratedSource must map that failure
# back to the specific CallFunction node, not just an opaque source
# line. See run_graph_diagnostics_test.cmake's own comment.
add_test(
    NAME gs11_graph_diagnostics_mapping
    COMMAND ${CMAKE_COMMAND}
            -DCELC_EXE=$<TARGET_FILE:celc>
            -DTEST_CELG=${CMAKE_CURRENT_SOURCE_DIR}/tests/nodegen/diagnostic_mapping.celg
            -DEXPECTED_FILE=${CMAKE_CURRENT_SOURCE_DIR}/tests/nodegen/diagnostic_mapping.expected
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_graph_diagnostics_test.cmake"
)

# GS11: --trace emits a genuine runtime execution trace (one real
# log_int call per node actually reached) -- see
# run_graph_trace_test.cmake's own comment.
add_test(
    NAME gs11_graph_trace
    COMMAND ${CMAKE_COMMAND}
            -DCELC_EXE=$<TARGET_FILE:celc>
            -DTEST_CELG=${CMAKE_CURRENT_SOURCE_DIR}/tests/nodegen/trace_demo.celg
            -DEXPECTED_FILE=${CMAKE_CURRENT_SOURCE_DIR}/tests/nodegen/trace_demo.expected
            -DTICKS=1
            -DDT=0.016666667
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_graph_trace_test.cmake"
)
