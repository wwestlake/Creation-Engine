#pragma once

#include <optional>
#include <string>
#include <vector>

#include "node_system/graph.h"
#include "node_system/type_registry.h"

namespace ce::node_system {

// Exec-cycle detection: DFS over Exec-kind connections only (Data
// connections are ignored here -- see TopologicalDataOrder for those),
// using a recursion stack. A cycle among Exec wires is unconditionally
// invalid: looping constructs (a "While" node, GS9's node catalog) have
// their own internal control-flow semantics, not something the exec
// WIRING between separate nodes should ever express by looping back on
// itself. Returns the cyclic node path (first node repeated at the end)
// if one exists, nullopt if the exec subgraph is acyclic.
std::optional<std::vector<NodeId>> DetectExecCycle(const Graph& graph);

// Topological order over Data-pin dependencies (an output must be
// computed before anything consuming it via a Data connection).
// Returns nullopt if a data cycle exists -- unlike Exec, a data cycle is
// unconditionally invalid; there's no dedicated node type that makes
// circular data dependencies meaningful the way a loop node makes an
// exec cycle-shaped control-flow pattern meaningful.
std::optional<std::vector<NodeId>> TopologicalDataOrder(const Graph& graph);

struct ValidationResult {
    bool ok = true;
    std::vector<std::string> errors;
};

// One combined structural pass: exec-cycle detection, data-cycle
// detection (via TopologicalDataOrder), and -- if `registry` is
// non-null -- ValidateAgainstRegistry. This is the "author-time
// validation" GS8 exists to provide; GS10's node editor is expected to
// run this after every edit and surface `errors` inline (per
// CAPABILITIES section 4.1's "flagged at author time," not deferred to
// compile/runtime).
ValidationResult ValidateGraph(const Graph& graph, const NodeTypeRegistry* registry = nullptr);

} // namespace ce::node_system
