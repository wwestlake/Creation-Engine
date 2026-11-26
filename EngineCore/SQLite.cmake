include(FetchContent)
if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()

FetchContent_Declare(djehuti_sqlite_source
    URL https://sqlite.org/2026/sqlite-amalgamation-3540100.zip
    URL_HASH SHA3_256=92281c86e2957f9b9305890f9ac04204caa7b677cc868dc326d942689df28e30
)
FetchContent_MakeAvailable(djehuti_sqlite_source)

add_library(djehuti_sqlite STATIC "${djehuti_sqlite_source_SOURCE_DIR}/sqlite3.c")
target_include_directories(djehuti_sqlite PUBLIC "${djehuti_sqlite_source_SOURCE_DIR}")
target_compile_definitions(djehuti_sqlite PRIVATE
    SQLITE_THREADSAFE=1
    SQLITE_OMIT_LOAD_EXTENSION
)
target_link_libraries(djehuti_sqlite PRIVATE Threads::Threads)
if(UNIX)
    target_link_libraries(djehuti_sqlite PRIVATE m)
endif()
