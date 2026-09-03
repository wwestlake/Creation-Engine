#pragma once

#include <set>
#include <string>
#include <vector>

#include <creation/frust/PluginRuntime.h>
#include "node_system/node_library.h"

namespace ce::frust {

// supportedCapabilities is the real fix for a gap found while wiring up
// the Node/Behavior Graph Foundations plan Phase 8: requiredCapabilities
// was already parsed off every node's manifest entry, but nothing ever
// checked it against anything -- a node library could declare it needed
// a capability the host doesn't actually provide and load anyway. A
// library with ANY node requiring a capability not in this set is
// rejected with a clear error instead of registering unenforced.
bool RegisterPluginNodeLibraries(const std::vector<creation::frust::PluginRuntime::NodeLibraryManifest>& manifests,
                                 node_system::NodeLibraryRegistry& registry,
                                 const std::set<std::string, std::less<>>& supportedCapabilities,
                                 std::string& error);

} // namespace ce::frust
