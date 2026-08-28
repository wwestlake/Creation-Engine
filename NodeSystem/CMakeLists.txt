add_library(node_system STATIC
    src/node.cpp
    src/graph.cpp
    src/type_registry.cpp
    src/graph_analysis.cpp
    src/frgraph_serialization.cpp
)
target_include_directories(node_system PUBLIC include)
target_compile_features(node_system PUBLIC cxx_std_20)
