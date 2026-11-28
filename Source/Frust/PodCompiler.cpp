#include "PodCatalog.h"

#include <node_system/core_control_flow.h>
#include <node_system/frust_codegen.h>
#include <node_system/node_library.h>

#include <algorithm>
#include <map>
#include <set>
#include <sstream>

namespace ce::frust {

bool CompilePodSource(const std::string& name, const PodEntry& entry, const node_system::Graph& graph,
                      const node_system::NodeLibraryRegistry& libraries, std::string& source, std::string& error)
{
    if (entry.authoringMode == PodAuthoringMode::Source) {
        source = entry.sourceText.toStdString();
        if (!entry.sourceText.trim().isEmpty()) return true;
        error = "The Pod source is empty.";
        return false;
    }

    // Base options shared by every function this Pod compiles to --
    // per-function fields (functionName/entryNode/resultNode/
    // emitManifestAndImports) are set per compile call below.
    node_system::FrustGraphCompileOptions baseOptions;
    baseOptions.manifestJson = "{\"name\":\"" + name + "\",\"version\":\"0.1.0\"}";
    baseOptions.exposeAsNode = entry.exposeAsNode;
    for (const auto& [id, library] : libraries.Libraries())
        baseOptions.sourceModules.insert(baseOptions.sourceModules.end(), library.frustSourceModules.begin(),
                                          library.frustSourceModules.end());
    // Explicit interface (Phase 6) applies to every function compiled
    // below, event-driven or not -- it declares this Pod's parameters,
    // not any one function's.
    for (const auto& input : entry.interfaceInputs) {
        baseOptions.parameters.push_back({ input.name.toStdString(), input.type });
        if (input.boundNode != 0)
            baseOptions.inputBindings.push_back({ input.boundNode, input.boundPin, input.name.toStdString() });
    }

    // Event nodes (On Tick/On Begin Play/On End Play, Phase 4) give
    // execution an explicit, visible start -- each one found compiles to
    // its own real lifecycle-hook function, concatenated into one .frust
    // file (first compile emits the shared manifest/imports header, the
    // rest skip it). Falls back to the pre-Phase-4 single-function
    // behavior below when the graph has no Event node at all, so every
    // Pod created before this phase keeps compiling exactly as it did.
    std::vector<node_system::NodeId> eventNodes;
    for (const auto& [id, node] : graph.Nodes())
        if (!node_system::EventNodeFrustFunctionName(node->TypeName()).empty()) eventNodes.push_back(id);

    std::ostringstream combinedSource;
    if (!eventNodes.empty()) {
        // Each function compiles with its own manifest/imports suppressed
        // (emitManifestAndImports = false for all of them, not just all-
        // but-the-first) -- extern declarations returned by different
        // compiles need deduping ACROSS every call before any of them are
        // emitted, which isn't possible if one call already baked its own
        // copy into `source`. The shared header (manifest + imports +
        // deduped externs) is assembled here instead, once, up front.
        std::map<std::string, std::string> externsByName;
        std::vector<std::string> functionBodies;
        for (size_t i = 0; i < eventNodes.size(); ++i) {
            const auto* node = graph.FindNode(eventNodes[i]);
            auto options = baseOptions;
            options.functionName = node_system::EventNodeFrustFunctionName(node->TypeName());
            options.entryNode = eventNodes[i];
            options.emitManifestAndImports = false;

            const auto result = node_system::CompileBehaviorGraphToFrust(graph, libraries, options);
            if (!result.ok) {
                error = options.functionName + ": " + result.error;
                return false;
            }
            functionBodies.push_back(result.source);
            for (const auto& decl : result.externDeclarations) externsByName[decl] = decl;
        }

        if (!baseOptions.manifestJson.empty())
            combinedSource << "manifest \"" << node_system::EscapeFrustString(baseOptions.manifestJson) << "\";\n\n";
        std::set<std::string> uniqueModules(baseOptions.sourceModules.begin(), baseOptions.sourceModules.end());
        for (const auto& module : uniqueModules) combinedSource << "use self::" << module << ";\n";
        if (!uniqueModules.empty()) combinedSource << "\n";
        for (const auto& [key, decl] : externsByName) { (void)key; combinedSource << decl; }
        if (!externsByName.empty()) combinedSource << "\n";
        for (const auto& body : functionBodies) combinedSource << body << "\n";
    } else {
        auto options = baseOptions;
        // Every compiled Behavior exposes one hook today: on_tick. Other
        // lifecycle hooks (on_spawn, on_begin_play, ...) are real,
        // documented (FRUST_BEHAVIOR_LIFECYCLE.md) but only reachable
        // from a graph via an explicit Event node (above) -- this
        // fallback path predates that and only ever produces on_tick.
        options.functionName = "on_tick";

        const bool hasExplicitInterface = !entry.interfaceInputs.empty() || entry.outputNode != 0;
        if (hasExplicitInterface) {
            options.resultNode = entry.outputNode;
            options.resultPin = entry.outputPin;
        } else {
            // Auto-detect resultNode: first node with a Data output.
            // Auto-detect entryNode: first node with an Exec input that
            // nothing else feeds -- the natural start of the exec chain.
            // Both are the "first found" heuristic this panel used
            // before Phase 6's explicit interface editor existed.
            for (const auto& [id, node] : graph.Nodes()) {
                if (!node->Outputs().empty() && node->Outputs().front().type.kind == node_system::PinKind::Data) {
                    options.resultNode = id;
                    options.resultPin = node->Outputs().front().id;
                    break;
                }
            }
            for (const auto& [id, node] : graph.Nodes()) {
                const auto execInput = std::find_if(node->Inputs().begin(), node->Inputs().end(),
                                                     [](const node_system::Pin& pin) { return pin.type.kind == node_system::PinKind::Exec; });
                if (execInput == node->Inputs().end()) continue;
                const bool hasIncoming = std::any_of(graph.Connections().begin(), graph.Connections().end(),
                                                      [&](const node_system::Connection& c) { return c.toNode == id && c.toPin == execInput->id; });
                if (!hasIncoming) { options.entryNode = id; break; }
            }
        }

        if (options.resultNode == 0 && options.entryNode == 0) {
            error = "Add at least one data-producing or executable node to compile this Pod.";
            return false;
        }

        // Same reasoning as the Event-node path above: build the header
        // ourselves so a host-extern node used outside any Event chain
        // (e.g. a Processing Pod reading a Variable) still gets its
        // extern fn declaration emitted -- the compiler itself never
        // embeds these in `source` anymore, only returns them as data.
        options.emitManifestAndImports = false;
        const auto result = node_system::CompileBehaviorGraphToFrust(graph, libraries, options);
        if (!result.ok) {
            error = result.error;
            return false;
        }
        if (!options.manifestJson.empty())
            combinedSource << "manifest \"" << node_system::EscapeFrustString(options.manifestJson) << "\";\n\n";
        std::set<std::string> uniqueModules(options.sourceModules.begin(), options.sourceModules.end());
        for (const auto& module : uniqueModules) combinedSource << "use self::" << module << ";\n";
        if (!uniqueModules.empty()) combinedSource << "\n";
        for (const auto& decl : result.externDeclarations) combinedSource << decl;
        if (!result.externDeclarations.empty()) combinedSource << "\n";
        combinedSource << result.source;
    }

    source = combinedSource.str();
    return true;
}

} // namespace ce::frust
