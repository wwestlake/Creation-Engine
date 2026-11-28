#pragma once

#include "DeveloperApi/RuntimeCommands.h"
#include "Runtime/RuntimeWorldRunner.h"

namespace ce::runtime
{
// Test data is opt-in through the existing Developer API fixture permission.
RuntimeGameSource DeveloperFixture();
developer::Value RuntimeDiagnostics(const RuntimeWorldRunner& runner, engine::World& world, bool fixture);
} // namespace ce::runtime
