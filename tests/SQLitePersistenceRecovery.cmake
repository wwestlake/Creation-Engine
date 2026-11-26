if(NOT DEFINED TEST_EXECUTABLE OR NOT DEFINED TEST_ROOT)
    message(FATAL_ERROR "SQLite recovery test executable and scratch root are required")
endif()

string(RANDOM LENGTH 16 ALPHABET 0123456789abcdef run_id)
set(run "${TEST_ROOT}/eng-002-recovery-${run_id}")
if(EXISTS "${run}")
    message(FATAL_ERROR "SQLite recovery test directory already exists")
endif()
file(MAKE_DIRECTORY "${run}")

function(check_child mode directory expected)
    execute_process(
        COMMAND "${TEST_EXECUTABLE}" "${mode}" "${directory}"
        RESULT_VARIABLE result
        OUTPUT_VARIABLE output
        ERROR_VARIABLE error
        TIMEOUT 30
    )
    if(NOT result STREQUAL expected)
        message(FATAL_ERROR "${mode} returned ${result}, expected ${expected}\n${output}\n${error}\nScratch data: ${run}")
    endif()
endfunction()

check_child(seed-full "${run}/full" 71)
check_child(verify "${run}/full" 0)
check_child(interrupt "${run}/full" 72)
check_child(verify "${run}/full" 0)
check_child(seed-normal "${run}/normal" 71)
check_child(verify "${run}/normal" 0)
check_child(exercise "${run}/integrity" 0)
file(REMOVE_RECURSE "${run}")
message(STATUS "SQLite process restart, interrupted transaction, backup, snapshot and failure tests passed")
