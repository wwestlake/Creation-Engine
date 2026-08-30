#pragma once

#include "Render/Import/GltfLoader.h"

namespace ce {

bool LoadFbx(const juce::File& fbxFile, LoadedModel& outModel, juce::String& errorMessage);

} // namespace ce
