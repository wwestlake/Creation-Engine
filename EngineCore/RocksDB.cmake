include(FetchContent)
if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()

function(creation_engine_add_rocksdb)
    # Use the Engine's existing zlib package and leave optional upstream
    # programs and dependencies out of the native provider build.
    set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
    set(ROCKSDB_BUILD_SHARED OFF)
    set(ROCKSDB_SKIP_THIRDPARTY ON)
    set(WITH_WINDOWS_UTF8_FILENAMES ON)
    set(WITH_TESTS OFF)
    set(WITH_TOOLS OFF)
    set(WITH_BENCHMARK_TOOLS OFF)
    set(WITH_CORE_TOOLS OFF)
    set(WITH_TRACE_TOOLS OFF)
    set(WITH_GFLAGS OFF)
    set(WITH_LIBURING OFF)
    set(WITH_JNI OFF)
    set(WITH_SNAPPY OFF)
    set(WITH_LZ4 OFF)
    set(WITH_ZLIB OFF)
    set(WITH_ZSTD OFF)
    set(FAIL_ON_WARNINGS OFF)

    FetchContent_Declare(djehuti_rocksdb_source
        URL https://codeload.github.com/facebook/rocksdb/tar.gz/refs/tags/v11.13.2
        URL_HASH SHA256=6f12f408c4876175b1de73cfefcdd1baf0001ad8d799c1a09320d96e4fd213b5
    )
    FetchContent_MakeAvailable(djehuti_rocksdb_source)
    target_compile_definitions(rocksdb PRIVATE ZLIB)
    target_link_libraries(rocksdb PRIVATE ZLIB::ZLIB)
endfunction()

creation_engine_add_rocksdb()
