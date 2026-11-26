#pragma once

#include "engine/persistence.h"

namespace ce::persistence
{
// Reference provider. Closed sessions can be reopened within this provider's
// lifetime. Committed records and journal entries are held in process memory.
std::unique_ptr<StorageProvider> CreateInMemoryProvider();
} // namespace ce::persistence
