#pragma once

#include <JuceHeader.h>

namespace ce::branding {

// Same visual language as Creation Station's branding (dark rounded
// badge, cyan/purple/pink accent gradient — see Creation Station's
// Branding.cpp for the reference this deliberately echoes), but its own
// glyph: an isometric wireframe cube instead of a waveform, fitting a
// 3D/node-based engine rather than an audio tool.
juce::Image CreateLogoImage(int size);

} // namespace ce::branding
