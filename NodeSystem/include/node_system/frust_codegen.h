#pragma once

#include <string>
#include <vector>

#include "node_system/node_library.h"

namespace ce::node_system {

// The behavior compiler deliberately emits ordinary FRust source.  It does
// not execute graph nodes itself: the generated root source imports the node
// library modules and is compiled by FRust as one plugin.
struct FrustGraphParameter {
    std::string name;
    DataType type = DataType::Int;
};

struct FrustGraphInputBinding {
    NodeId node = 0;
    PinId pin = 0;
    std::string parameterName;
};

struct FrustGraphCompileOptions {
    std::string functionName;
    std::vector<FrustGraphParameter> parameters;
    std::vector<FrustGraphInputBinding> inputBindings;
    NodeId resultNode = 0;
    PinId resultPin = 0;
    // Optional event entry. When supplied, callable nodes reached through
    // their `then` exec pins are emitted as ordered FRust calls before the
    // graph's data result is returned.
    NodeId entryNode = 0;
    // Names used in `use self::<name>;`.  The generated root must be saved
    // next to those .frust source modules before frust_plugin_load() runs.
    std::vector<std::string> sourceModules;
    std::string manifestJson;
};

struct FrustGraphCompileResult {
    bool ok = false;
    std::string source;
    std::string error;
};

FrustGraphCompileResult CompileBehaviorGraphToFrust(const Graph& graph,
                                                     const NodeLibraryRegistry& libraries,
                                                     const FrustGraphCompileOptions& options);

} // namespace ce::node_system
