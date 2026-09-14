#pragma once

#include <functional>
#include <vector>

#include <JuceHeader.h>

namespace ce::views {

struct OpenDocumentChoice
{
    juce::String id;
    juce::String name;
    juce::String detail;
};

// Reusable list picker for durable Engine documents. The caller supplies
// stable ids; the dialog never treats a user-editable display name as an id.
void showOpenDocumentDialog(juce::String title,
                            juce::String prompt,
                            std::vector<OpenDocumentChoice> choices,
                            std::function<void(juce::String selectedId)> onOpen);

} // namespace ce::views
