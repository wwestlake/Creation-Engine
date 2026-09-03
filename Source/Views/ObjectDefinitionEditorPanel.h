#pragma once

#include <JuceHeader.h>
#include <creation/assets/ProjectSession.h>

#include "Frust/PodCatalog.h"
#include "Scene/ObjectDefinitions.h"

namespace ce::views
{

// A small editor for ONE Object Definition (docs/OBJECT_MODEL.md) -- one
// uniform list of components (a mesh reference, an attached Pod, or a
// nested child object are all the same kind of list entry, per the
// settled component model), plus Save. No node graph: unlike the Pod
// editor, an Object Definition has no logic of its own to wire, it's a
// recipe. Lazily opened/closed by MainComponent exactly like the Pod
// editor -- never a standing dock tab.
class ObjectDefinitionEditorPanel final : public juce::Component
{
public:
    ObjectDefinitionEditorPanel(scene::ObjectDefinitionCatalog& catalog, frust::PodCatalog& podCatalog,
                                creation::assets::ProjectSession& projectSession);
    // Declared (not defaulted inline) and defined in the .cpp, after
    // ComponentRow's full definition -- same reason PodEditorPanel's own
    // destructor is out-of-line: OwnedArray<ComponentRow>'s destructor
    // can't be instantiated against ComponentRow as an incomplete
    // (forward-declared) type.
    ~ObjectDefinitionEditorPanel() override;

    // The only way anything ever appears in this panel, same shape as
    // PodEditorPanel::OpenPod.
    void OpenDefinition(const juce::String& id);

    void resized() override;
    void paint(juce::Graphics& g) override;

private:
    // Shows the Kind-choice popup (Mesh Reference / Pod / Child Object)
    // and dispatches to whichever of the three below the user picks.
    void AddComponent();
    void PickMesh();
    void AddPod();
    void PickChildDefinition();
    void RemoveComponentAt(int index);
    void SaveContent();
    void RefreshComponentRows();

    scene::ObjectDefinitionCatalog& catalog_;
    frust::PodCatalog& podCatalog_;
    creation::assets::ProjectSession& projectSession_;

    juce::String openId_;
    std::vector<scene::ObjectComponentEntry> components_;

    juce::Label titleLabel_{ {}, "Object Definition" };
    juce::Label nameLabel_;

    juce::ToggleButton editorOnlyToggle_{ "Editor only (excluded from Play)" };

    juce::Label componentsSectionLabel_{ {}, "Components" };
    juce::TextButton addComponentButton_{ "+ Add Component" };
    class ComponentRow;
    juce::OwnedArray<ComponentRow> componentRows_;

    juce::TextButton saveButton_{ "Save" };
    juce::Label status_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ObjectDefinitionEditorPanel)
};

} // namespace ce::views
