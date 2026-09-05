#pragma once

#include <functional>

#include <JuceHeader.h>
#include <creation/assets/ProjectSession.h>

#include "Input/InputActionSystem.h"
#include "Input/InputBindingDocumentStore.h"
#include "Input/StarterInputMappingPresets.h"
#include "Project/EngineGameDocument.h"

namespace ce::views
{
// Authors the one InputBindings document belonging to whichever Game is
// currently active -- not a browse/catalog panel (there is exactly one
// document, not many "Things" to list; see docs/OBJECT_MODEL.md's
// Entity/Thing/Object section on why this doesn't get the
// create/list/open/save/delete/rename treatment PodCatalog/
// ObjectDefinitionCatalog give real Things, same reasoning games.xml
// itself isn't one). Opened/closed on demand from the View menu
// (MainComponent::EnsureInputBindingsPanelOpen), same lazy register/
// unregister-with-close-button shape PodEditorPanel already uses -- unlike
// Pods it IS listed in the View menu (there's exactly one of it, not one
// per opened asset), it just isn't part of the app's default always-docked
// layout.
class InputBindingsPanel final : public juce::Component
{
public:
    InputBindingsPanel(input::InputActionSystem& inputActionSystem, creation::assets::ProjectSession& projectSession);

    // Declared here, defined out-of-line in the .cpp after ActionRow/
    // BindingRow's full definitions are visible -- same reason
    // BehaviorAttachmentPanel::~BehaviorAttachmentPanel() is out-of-line
    // (rows_ is a juce::OwnedArray<ActionRow> where ActionRow is only
    // forward-declared here).
    ~InputBindingsPanel() override;

    // Called from the same two MainComponent call sites that already call
    // InputActionSystem::LoadForGame (openActiveGame/selectGame) -- keeps
    // this panel's displayed rows in lockstep with whichever Game is
    // active.
    void SetActiveGame(const project::GameDocumentInfo& game);
    void Refresh();

    // Set by MainComponent right after construction -- called whenever a
    // Combo is added/renamed/removed and saved, so the corresponding
    // Schematic event node (Input Combo Events plan) stays in sync. A
    // plain callback rather than a direct EngineFrustHost& reference,
    // matching this codebase's existing cross-panel wiring convention
    // (e.g. ImportPanel::onContentChanged).
    std::function<void()> onCombosChanged;

    void resized() override;
    void paint(juce::Graphics& g) override;

private:
    class ActionRow;
    class BindingRow;
    class CaptureOverlay;
    class ComboRow;
    class ComboCaptureOverlay;

    void AddAction();
    void LoadPreset(const input::InputMappingPreset& preset);
    void RemoveAction(const juce::String& name);
    void SetActionKind(const juce::String& name, input::ActionKind kind);
    void AddBinding(const juce::String& actionName);
    void RemoveBinding(const juce::String& actionName, int bindingIndex);
    void BeginCapture(BindingRow& target);
    void FinishCapture(const input::InputBinding& captured);
    void CancelCapture();

    void AddCombo();
    void RecordCombo(const juce::String& existingName);
    void RenameCombo(const juce::String& name);
    void RemoveCombo(const juce::String& name);
    void FinishComboCapture(const juce::Array<input::ComboKeyPress>& recorded);
    void CancelComboCapture();

    void SaveContent();

    input::InputActionSystem& inputActionSystem_;
    creation::assets::ProjectSession& projectSession_;
    project::GameDocumentInfo activeGame_;

    juce::Label titleLabel_ { {}, "Input Bindings" };
    juce::TextButton addActionButton_ { "Add Action" };
    juce::TextButton loadPresetButton_ { "Load Preset" };
    juce::TextButton addComboButton_ { "Add Combo" };
    juce::TextButton saveButton_ { "Save" };
    juce::Label statusLabel_;
    juce::OwnedArray<ActionRow> rows_;
    juce::Label comboSectionLabel_ { {}, "Combos" };
    juce::OwnedArray<ComboRow> comboRows_;
    std::unique_ptr<CaptureOverlay> captureOverlay_; // non-null only while actively capturing a rebind.
    juce::String capturingActionName_;
    int capturingBindingIndex_ = -1;
    std::unique_ptr<ComboCaptureOverlay> comboCaptureOverlay_; // non-null only while recording a combo.
    juce::String comboBeingRecorded_; // empty = recording a brand-new combo (prompts for a name after); non-empty = re-recording that existing combo's keys.

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(InputBindingsPanel)
};
} // namespace ce::views
