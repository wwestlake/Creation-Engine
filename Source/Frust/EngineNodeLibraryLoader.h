#pragma once

#include <string>
#include <vector>

#include <creation/frust/PluginRuntime.h>
#include "node_system/node_library.h"

namespace ce::frust {

bool RegisterPluginNodeLibraries(const std::vector<creation::frust::PluginRuntime::NodeLibraryManifest>& manifests,
                                 node_system::NodeLibraryRegistry& registry, std::string& error);

} // namespace ce::frust
