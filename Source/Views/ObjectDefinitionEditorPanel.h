#pragma once

#include <JuceHeader.h>
#include <creation/assets/ProjectSession.h>

#include "Frust/PodCatalog.h"
#include "Scene/ObjectDefinitions.h"

namespace ce::views
{

// A small editor for ONE Object Definition (docs/OBJECT_MODEL.md) -- a
// mesh-asset picker, an attach/detach list of Pod ids, and Save. No node
// graph: unlike the Pod editor, an Object Definition has no logic of its
// own to wire, it's a recipe (mesh + materials, via the mesh asset's own
// bundled material + attached behavior Pods). Lazily opened/closed by
// MainComponent exactly like the Pod editor -- never a standing dock tab.
// The VR Editor Cart plan, Phase 1.
class ObjectDefinitionEditorPanel final : public juce::Component
{
public:
    ObjectDefinitionEditorPanel(scene::ObjectDefinitionCatalog& catalog, frust::PodCatalog& podCatalog,
                                creation::assets::ProjectSession& projectSession);
    // Declared (not defaulted inline) and defined in the .cpp, after
    // PodRow's full definition -- same reason PodEditorPanel's own
    // destructor is out-of-line: OwnedArray<PodRow>'s destructor can't be
    // instantiated against PodRow as an incomplete (forward-declared) type.
    ~ObjectDefinitionEditorPanel() override;

    // The only way anything ever appears in this panel, same shape as
    // PodEditorPanel::OpenPod.
    void OpenDefinition(const juce::String& id);

    void resized() override;
    void paint(juce::Graphics& g) override;

private:
    void PickMesh();
    void AddPod();
    void RemovePodAt(int index);
    void SaveContent();
    void RefreshPodRows();

    scene::ObjectDefinitionCatalog& catalog_;
    frust::PodCatalog& podCatalog_;
    creation::assets::ProjectSession& projectSession_;

    juce::String openId_;
    juce::String meshDisplayName_; // resolved for display only; the real reference is meshAssetId_/meshAssetVersionId_.
    juce::String meshAssetId_;
    juce::String meshAssetVersionId_;

    juce::Label titleLabel_{ {}, "Object Definition" };
    juce::Label nameLabel_;

    juce::Label meshSectionLabel_{ {}, "Mesh" };
    juce::Label meshLabel_{ {}, "(none)" };
    juce::TextButton pickMeshButton_{ "Choose Mesh..." };

    juce::Label podsSectionLabel_{ {}, "Attached Pods" };
    juce::TextButton addPodButton_{ "+ Add Pod" };
    class PodRow;
    juce::OwnedArray<PodRow> podRows_;
    std::vector<juce::String> attachedPods_;

    juce::TextButton saveButton_{ "Save" };
    juce::Label status_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ObjectDefinitionEditorPanel)
};

} // namespace ce::views
