#pragma once

#include <string>
#include <vector>

#include "node_system/graph.h"
#include "node_system/type_registry.h"

// GS9: graph -> CEL textual source generation. Deliberately produces a
// std::string of real .cel source text rather than constructing an AST
// (ce::lang::Program) directly, per docs/GS_SCRIPTING_PLAN.md's own
// architecture decision: the graph and the text language must be
// provably one language, sharing the identical parse/sema/IR-gen/JIT
// pipeline, not two AST-construction code paths that could drift apart.
// The generated text is meant to be readable and hand-editable (a
// future "View Generated Code" pane), not just internally consumed.
//
// This module (ce_lang_nodegen) depends on ce_lang_frontend and
// node_system only -- explicitly NOT ce_lang_jit/LLVM (see
// Language/CMakeLists.txt's own comment on the target chain). Actually
// running the generated text is celc's job (--run-graph), by handing
// the string to ce_lang_jit exactly as it would any other .cel file.

namespace ce::lang::nodegen {

struct GraphToSourceResult {
    bool ok = false;
    std::string source;
    std::vector<std::string> errors;
};

// Validates `graph` against `registry` (ValidateGraph -- exec/data
// cycles, registry conformance) and, if that passes, walks each
// OnStart/OnTick entry node's exec chain to emit one top-level
// `func on_start(self: entity)` / `func on_tick(self: entity, dt: float)`
// per entry node found (the same lifecycle CelScriptRuntime/
// Simulation::Step require -- see docs/SCRIPTING_ABI.md). Each emitted
// statement/block is preceded by a `// @node <id>` comment, the "source
// map" GS11's diagnostic-mapping-back-to-nodes goal will read.
//
// A graph with neither an OnStart nor an OnTick node, or with more than
// one of either, is a codegen error (not a crash) -- see `errors`.
GraphToSourceResult GenerateSource(const ce::node_system::Graph& graph, const ce::node_system::NodeTypeRegistry& registry);

} // namespace ce::lang::nodegen
