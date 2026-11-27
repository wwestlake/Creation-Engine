# Platform/framework-agnostic simulation core: entity/world state, tick
# clock, node-graph execution hookup. Deliberately has no JUCE dependency
# so it stays reusable outside the JUCE app shell if that's ever useful
# (e.g. a pure command-line tool), and so the dedicated server's core loop
# doesn't pull in anything GUI-related.
add_library(engine_core STATIC
    src/world.cpp
    src/simulation.cpp
    src/foundation_gameplay.cpp
    src/mock_vr_provider.cpp
    src/persistence.cpp
    src/persistence_query.cpp
    src/in_memory_persistence.cpp
)
target_include_directories(engine_core PUBLIC include)
target_link_libraries(engine_core PUBLIC EnTT::EnTT node_system)
target_compile_features(engine_core PUBLIC cxx_std_20)

include(SQLite.cmake)
add_library(engine_sqlite_persistence STATIC
    src/sqlite_persistence.cpp
    src/sqlite_persistence_database.cpp
)
target_link_libraries(engine_sqlite_persistence PUBLIC engine_core PRIVATE djehuti_sqlite)

include(RocksDB.cmake)
add_library(engine_rocksdb_persistence STATIC src/rocksdb_persistence.cpp)
target_link_libraries(engine_rocksdb_persistence PUBLIC engine_core PRIVATE rocksdb)
