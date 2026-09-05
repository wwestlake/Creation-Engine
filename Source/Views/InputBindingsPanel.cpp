#include "Views/InputBindingsPanel.h"

#include <algorithm>

namespace ce::views
{
using input::ActionKind;
using input::ComboKeyPress;
using input::InputAction;
using input::InputBinding;
using input::InputCombo;
using input::InputSourceKind;

namespace
{
juce::String Describe(const InputBinding& binding)
{
    switch (binding.sourceKind) {
        case InputSourceKind::KeyboardKey:
            return "Keyboard: " + juce::KeyPress(binding.code).getTextDescription();
        case InputSourceKind::MouseButton:
            return juce::String("Mouse: ") + (binding.code == 0 ? "Left" : binding.code == 1 ? "Right" : "Middle");
        case InputSourceKind::ControllerButton:
            return "Controller Button " + juce::String(binding.code);
        case InputSourceKind::ControllerAxis:
            return "Controller Axis " + juce::String(binding.code);
    }
    return "Unbound";
}

InputAction* FindAction(input::InputBindingSet& bindings, const juce::String& name)
{
    for (auto& action : bindings.actions)
        if (action.name == name) return &action;
    return nullptr;
}

InputCombo* FindCombo(input::InputBindingSet& bindings, const juce::String& name)
{
    for (auto& combo : bindings.combos)
        if (combo.name == name) return &combo;
    return nullptr;
}

juce::String DescribeCombo(const juce::Array<ComboKeyPress>& keys)
{
    if (keys.isEmpty()) return "Unbound";
    juce::String text;
    for (int i = 0; i < keys.size(); ++i) {
        if (i > 0) text += " -> ";
        text += juce::KeyPress(keys[i].keyCode).getTextDescription();
        if (keys[i].offsetMillis > 0) text += " (+" + juce::String(keys[i].offsetMillis) + "ms)";
    }
    return text;
}
} // namespace

// Full-panel overlay shown while capturing a rebind -- editor-authoring-
// time input, in an ordinary focused-UI moment, which is exactly where
// JUCE's routed keyPressed/mouseDown ARE reliable (FreeCamera.cpp's own
// comment explains the unreliability is specific to a hidden-cursor
// continuous-look moment, not an ordinary click-to-capture).
class InputBindingsPanel::CaptureOverlay final : public juce::Component
{
public:
    explicit CaptureOverlay(InputBindingsPanel& owner) : owner_(owner)
    {
        setWantsKeyboardFocus(true);
        setInterceptsMouseClicks(true, false);
        label_.setText("Press a key or click a mouse button to bind (Esc to cancel)...",
                        juce::dontSendNotification);
        label_.setJustificationType(juce::Justification::centred);
        label_.setColour(juce::Label::textColourId, juce::Colours::white);
        addAndMakeVisible(label_);
    }

    void resized() override { label_.setBounds(getLocalBounds()); }
    void paint(juce::Graphics& g) override { g.fillAll(juce::Colour(0xcc101215)); }

    void visibilityChanged() override
    {
        if (isVisible()) grabKeyboardFocus();
    }

    bool keyPressed(const juce::KeyPress& key) override
    {
        if (key == juce::KeyPress::escapeKey) {
            owner_.CancelCapture();
            return true;
        }
        owner_.FinishCapture({ InputSourceKind::KeyboardKey, key.getKeyCode(), 1000 });
        return true;
    }

    void mouseDown(const juce::MouseEvent& event) override
    {
        const int code = event.mods.isLeftButtonDown() ? 0 : event.mods.isRightButtonDown() ? 1 : 2;
        owner_.FinishCapture({ InputSourceKind::MouseButton, code, 1000 });
    }

private:
    InputBindingsPanel& owner_;
    juce::Label label_;
};

class InputBindingsPanel::BindingRow final : public juce::Component
{
public:
    BindingRow(InputBindingsPanel& owner, juce::String actionName, int bindingIndex, const InputBinding& binding)
        : owner_(owner), actionName_(std::move(actionName)), bindingIndex_(bindingIndex)
    {
        descriptionLabel_.setText(Describe(binding), juce::dontSendNotification);
        descriptionLabel_.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
        addAndMakeVisible(descriptionLabel_);

        rebindButton_.onClick = [this] { owner_.BeginCapture(*this); };
        addAndMakeVisible(rebindButton_);

        removeButton_.onClick = [this] { owner_.RemoveBinding(actionName_, bindingIndex_); };
        addAndMakeVisible(removeButton_);
    }

    const juce::String& ActionName() const { return actionName_; }
    int BindingIndex() const { return bindingIndex_; }

    void resized() override
    {
        auto bounds = getLocalBounds();
        removeButton_.setBounds(bounds.removeFromRight(70).reduced(2));
        rebindButton_.setBounds(bounds.removeFromRight(80).reduced(2));
        descriptionLabel_.setBounds(bounds);
    }

private:
    InputBindingsPanel& owner_;
    juce::String actionName_;
    int bindingIndex_;
    juce::Label descriptionLabel_;
    juce::TextButton rebindButton_ { "Rebind" };
    juce::TextButton removeButton_ { "Remove" };
};

class InputBindingsPanel::ActionRow final : public juce::Component
{
public:
    ActionRow(InputBindingsPanel& owner, const InputAction& action) : owner_(owner), actionName_(action.name)
    {
        nameLabel_.setText(action.name, juce::dontSendNotification);
        nameLabel_.setFont(juce::Font(juce::FontOptions(14.0f)).boldened());
        nameLabel_.setColour(juce::Label::textColourId, juce::Colours::white);
        addAndMakeVisible(nameLabel_);

        kindCombo_.addItem("Digital", 1);
        kindCombo_.addItem("Analog", 2);
        kindCombo_.setSelectedId(action.kind == ActionKind::Digital ? 1 : 2, juce::dontSendNotification);
        kindCombo_.onChange = [this] {
            owner_.SetActionKind(actionName_, kindCombo_.getSelectedId() == 1 ? ActionKind::Digital : ActionKind::Analog);
        };
        addAndMakeVisible(kindCombo_);

        addBindingButton_.onClick = [this] { owner_.AddBinding(actionName_); };
        addAndMakeVisible(addBindingButton_);

        removeActionButton_.onClick = [this] { owner_.RemoveAction(actionName_); };
        addAndMakeVisible(removeActionButton_);

        for (int i = 0; i < action.bindings.size(); ++i) {
            auto* row = bindingRows_.add(new BindingRow(owner_, actionName_, i, action.bindings[i]));
            addAndMakeVisible(row);
        }
    }

    // Total height this row needs, including its binding rows -- computed
    // up front so the panel's resized() can lay out rows one after another
    // without a second pass.
    int PreferredHeight() const { return kHeaderHeight + bindingRows_.size() * kBindingRowHeight; }

    void resized() override
    {
        auto bounds = getLocalBounds();
        auto header = bounds.removeFromTop(kHeaderHeight);
        removeActionButton_.setBounds(header.removeFromRight(70).reduced(2));
        addBindingButton_.setBounds(header.removeFromRight(90).reduced(2));
        kindCombo_.setBounds(header.removeFromRight(100).reduced(2));
        nameLabel_.setBounds(header);

        auto indented = bounds;
        indented.removeFromLeft(16);
        for (auto* row : bindingRows_) {
            row->setBounds(indented.removeFromTop(kBindingRowHeight).reduced(0, 2));
        }
    }

    static constexpr int kHeaderHeight = 26;
    static constexpr int kBindingRowHeight = 24;

private:
    InputBindingsPanel& owner_;
    juce::String actionName_;
    juce::Label nameLabel_;
    juce::ComboBox kindCombo_;
    juce::TextButton addBindingButton_ { "Add Binding" };
    juce::TextButton removeActionButton_ { "Remove" };
    juce::OwnedArray<BindingRow> bindingRows_;
};

// Full-panel overlay shown while recording a combo -- accumulates up to 3
// keys with their elapsed-time offsets (the exact data InputActionSystem's
// matcher consumes, see InputActionSystem.h's own comment on why a chord
// and a short sequence are just the same recorded shape with different
// offsets). Stops on 3 keys, a ~600ms settle timeout after the most
// recent key, an explicit Done click, or Escape to cancel -- matching
// "I click a box and then press key combos, it records and displays
// them," no separate record-mode toggle.
class InputBindingsPanel::ComboCaptureOverlay final : public juce::Component, private juce::Timer
{
public:
    explicit ComboCaptureOverlay(InputBindingsPanel& owner) : owner_(owner)
    {
        setWantsKeyboardFocus(true);
        label_.setJustificationType(juce::Justification::centred);
        label_.setColour(juce::Label::textColourId, juce::Colours::white);
        UpdateLabel();
        addAndMakeVisible(label_);

        doneButton_.onClick = [this] { Finish(); };
        addAndMakeVisible(doneButton_);
    }

    void resized() override
    {
        auto bounds = getLocalBounds();
        auto buttonRow = bounds.removeFromBottom(36);
        doneButton_.setBounds(buttonRow.withSizeKeepingCentre(120, 28));
        label_.setBounds(bounds);
    }
    void paint(juce::Graphics& g) override { g.fillAll(juce::Colour(0xcc101215)); }

    void visibilityChanged() override
    {
        if (isVisible()) grabKeyboardFocus();
    }

    bool keyPressed(const juce::KeyPress& key) override
    {
        if (key == juce::KeyPress::escapeKey) {
            owner_.CancelComboCapture();
            return true;
        }
        if (recorded_.size() >= 3) {
            return true; // already full -- ignore extras until Done/timeout.
        }
        const double now = juce::Time::getMillisecondCounterHiRes();
        if (recorded_.isEmpty()) firstKeyTimeMs_ = now;
        recorded_.add({ key.getKeyCode(), static_cast<int>(now - firstKeyTimeMs_) });
        UpdateLabel();
        startTimer(600); // settle countdown, reset by every subsequent keystroke.
        if (recorded_.size() >= 3) {
            Finish();
        }
        return true;
    }

private:
    void timerCallback() override
    {
        stopTimer();
        Finish();
    }

    void Finish()
    {
        stopTimer();
        if (recorded_.isEmpty()) {
            owner_.CancelComboCapture();
            return;
        }
        owner_.FinishComboCapture(recorded_);
    }

    void UpdateLabel()
    {
        juce::String text = "Press up to 3 keys (together, or in sequence with a short pause)...\n\n";
        text += DescribeCombo(recorded_);
        text += "\n\nClick Done to finish early, Esc to cancel.";
        label_.setText(text, juce::dontSendNotification);
    }

    InputBindingsPanel& owner_;
    juce::Label label_;
    juce::TextButton doneButton_ { "Done" };
    juce::Array<ComboKeyPress> recorded_;
    double firstKeyTimeMs_ = 0.0;
};

class InputBindingsPanel::ComboRow final : public juce::Component
{
public:
    ComboRow(InputBindingsPanel& owner, const InputCombo& combo) : owner_(owner), name_(combo.name)
    {
        nameLabel_.setText(combo.name, juce::dontSendNotification);
        nameLabel_.setFont(juce::Font(juce::FontOptions(14.0f)).boldened());
        nameLabel_.setColour(juce::Label::textColourId, juce::Colours::white);
        addAndMakeVisible(nameLabel_);

        descriptionLabel_.setText(DescribeCombo(combo.keys), juce::dontSendNotification);
        descriptionLabel_.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
        addAndMakeVisible(descriptionLabel_);

        recordButton_.onClick = [this] { owner_.RecordCombo(name_); };
        addAndMakeVisible(recordButton_);

        renameButton_.onClick = [this] { owner_.RenameCombo(name_); };
        addAndMakeVisible(renameButton_);

        removeButton_.onClick = [this] { owner_.RemoveCombo(name_); };
        addAndMakeVisible(removeButton_);
    }

    void resized() override
    {
        auto bounds = getLocalBounds();
        removeButton_.setBounds(bounds.removeFromRight(70).reduced(2));
        renameButton_.setBounds(bounds.removeFromRight(80).reduced(2));
        recordButton_.setBounds(bounds.removeFromRight(90).reduced(2));
        nameLabel_.setBounds(bounds.removeFromTop(bounds.getHeight() / 2));
        descriptionLabel_.setBounds(bounds);
    }

    static constexpr int kRowHeight = 44;

private:
    InputBindingsPanel& owner_;
    juce::String name_;
    juce::Label nameLabel_;
    juce::Label descriptionLabel_;
    juce::TextButton recordButton_ { "Re-record" };
    juce::TextButton renameButton_ { "Rename" };
    juce::TextButton removeButton_ { "Remove" };
};

InputBindingsPanel::InputBindingsPanel(input::InputActionSystem& inputActionSystem, creation::assets::ProjectSession& projectSession)
    : inputActionSystem_(inputActionSystem), projectSession_(projectSession)
{
    titleLabel_.setFont(juce::Font(juce::FontOptions(16.0f)).boldened());
    titleLabel_.setColour(juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible(titleLabel_);

    addActionButton_.onClick = [this] { AddAction(); };
    addAndMakeVisible(addActionButton_);

    addComboButton_.onClick = [this] { AddCombo(); };
    addAndMakeVisible(addComboButton_);

    saveButton_.onClick = [this] { SaveContent(); };
    addAndMakeVisible(saveButton_);

    statusLabel_.setColour(juce::Label::textColourId, juce::Colours::grey);
    addAndMakeVisible(statusLabel_);

    comboSectionLabel_.setFont(juce::Font(juce::FontOptions(13.0f)).boldened());
    comboSectionLabel_.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    addAndMakeVisible(comboSectionLabel_);
}

InputBindingsPanel::~InputBindingsPanel() = default;

void InputBindingsPanel::SetActiveGame(const project::GameDocumentInfo& game)
{
    activeGame_ = game;
    statusLabel_.setText({}, juce::dontSendNotification);
    Refresh();
}

void InputBindingsPanel::Refresh()
{
    rows_.clear();
    for (const auto& action : inputActionSystem_.Bindings().actions) {
        auto* row = rows_.add(new ActionRow(*this, action));
        addAndMakeVisible(row);
    }
    comboRows_.clear();
    for (const auto& combo : inputActionSystem_.Bindings().combos) {
        auto* row = comboRows_.add(new ComboRow(*this, combo));
        addAndMakeVisible(row);
    }
    comboSectionLabel_.setVisible(!comboRows_.isEmpty());
    resized();
}

void InputBindingsPanel::AddAction()
{
    auto* prompt = new juce::AlertWindow("Add Action", "Name for the new Action:", juce::MessageBoxIconType::QuestionIcon);
    prompt->addTextEditor("name", "NewAction");
    prompt->addButton("Add", 1);
    prompt->addButton("Cancel", 0);
    auto safeThis = juce::Component::SafePointer<InputBindingsPanel>(this);
    prompt->enterModalState(true, juce::ModalCallbackFunction::create([safeThis, prompt](int result) mutable {
        std::unique_ptr<juce::AlertWindow> owned(prompt);
        if (result != 1 || safeThis == nullptr) return;
        const auto name = owned->getTextEditorContents("name").trim();
        if (name.isEmpty()) return;
        auto& bindings = safeThis->inputActionSystem_.Bindings();
        if (FindAction(bindings, name) != nullptr) {
            safeThis->statusLabel_.setText("An Action named \"" + name + "\" already exists.", juce::dontSendNotification);
            return;
        }
        InputAction action;
        action.name = name;
        bindings.actions.add(std::move(action));
        safeThis->Refresh();
    }), true);
}

void InputBindingsPanel::RemoveAction(const juce::String& name)
{
    auto& actions = inputActionSystem_.Bindings().actions;
    for (int i = 0; i < actions.size(); ++i) {
        if (actions.getReference(i).name == name) {
            actions.remove(i);
            break;
        }
    }
    Refresh();
}

void InputBindingsPanel::SetActionKind(const juce::String& name, ActionKind kind)
{
    if (auto* action = FindAction(inputActionSystem_.Bindings(), name)) action->kind = kind;
}

void InputBindingsPanel::AddBinding(const juce::String& actionName)
{
    if (auto* action = FindAction(inputActionSystem_.Bindings(), actionName)) {
        action->bindings.add(InputBinding{});
        Refresh();
    }
}

void InputBindingsPanel::RemoveBinding(const juce::String& actionName, int bindingIndex)
{
    if (auto* action = FindAction(inputActionSystem_.Bindings(), actionName)) {
        if (bindingIndex >= 0 && bindingIndex < action->bindings.size()) action->bindings.remove(bindingIndex);
    }
    Refresh();
}

void InputBindingsPanel::BeginCapture(BindingRow& target)
{
    capturingActionName_ = target.ActionName();
    capturingBindingIndex_ = target.BindingIndex();
    captureOverlay_ = std::make_unique<CaptureOverlay>(*this);
    addAndMakeVisible(*captureOverlay_);
    captureOverlay_->setBounds(getLocalBounds());
}

void InputBindingsPanel::FinishCapture(const InputBinding& captured)
{
    if (auto* action = FindAction(inputActionSystem_.Bindings(), capturingActionName_)) {
        if (capturingBindingIndex_ >= 0 && capturingBindingIndex_ < action->bindings.size())
            action->bindings.getReference(capturingBindingIndex_) = captured;
    }
    CancelCapture(); // tears down the overlay; state was already applied above.
}

void InputBindingsPanel::CancelCapture()
{
    captureOverlay_.reset();
    capturingActionName_ = {};
    capturingBindingIndex_ = -1;
    Refresh();
}

void InputBindingsPanel::AddCombo()
{
    comboBeingRecorded_ = {}; // empty -- a brand-new combo, prompt for a name once recorded.
    comboCaptureOverlay_ = std::make_unique<ComboCaptureOverlay>(*this);
    addAndMakeVisible(*comboCaptureOverlay_);
    comboCaptureOverlay_->setBounds(getLocalBounds());
}

void InputBindingsPanel::RecordCombo(const juce::String& existingName)
{
    comboBeingRecorded_ = existingName; // non-empty -- re-recording this combo's keys, no rename prompt after.
    comboCaptureOverlay_ = std::make_unique<ComboCaptureOverlay>(*this);
    addAndMakeVisible(*comboCaptureOverlay_);
    comboCaptureOverlay_->setBounds(getLocalBounds());
}

void InputBindingsPanel::RenameCombo(const juce::String& name)
{
    auto* prompt = new juce::AlertWindow("Rename Combo", "New name for \"" + name + "\":", juce::MessageBoxIconType::QuestionIcon);
    prompt->addTextEditor("name", name);
    prompt->addButton("Rename", 1);
    prompt->addButton("Cancel", 0);
    auto safeThis = juce::Component::SafePointer<InputBindingsPanel>(this);
    prompt->enterModalState(true, juce::ModalCallbackFunction::create([safeThis, prompt, name](int result) mutable {
        std::unique_ptr<juce::AlertWindow> owned(prompt);
        if (result != 1 || safeThis == nullptr) return;
        const auto newName = owned->getTextEditorContents("name").trim();
        if (newName.isEmpty() || newName == name) return;
        auto& bindings = safeThis->inputActionSystem_.Bindings();
        if (FindCombo(bindings, newName) != nullptr) {
            safeThis->statusLabel_.setText("A Combo named \"" + newName + "\" already exists.", juce::dontSendNotification);
            return;
        }
        if (auto* combo = FindCombo(bindings, name)) {
            combo->name = newName;
            // Renaming changes the compiled hook name (EventNodeFrustFunctionName
            // derives it from the current name) -- re-derive the Schematic
            // node's identity immediately, same as add/remove below. Actual
            // disk persistence still waits for an explicit Save, matching
            // every Action mutation above.
            if (safeThis->onCombosChanged) safeThis->onCombosChanged();
        }
        safeThis->Refresh();
    }), true);
}

void InputBindingsPanel::RemoveCombo(const juce::String& name)
{
    auto& combos = inputActionSystem_.Bindings().combos;
    for (int i = 0; i < combos.size(); ++i) {
        if (combos.getReference(i).name == name) {
            combos.remove(i);
            break;
        }
    }
    if (onCombosChanged) onCombosChanged();
    Refresh();
}

void InputBindingsPanel::FinishComboCapture(const juce::Array<ComboKeyPress>& recorded)
{
    auto& bindings = inputActionSystem_.Bindings();
    if (comboBeingRecorded_.isNotEmpty()) {
        // Re-recording an existing combo's keys -- no name prompt needed.
        if (auto* combo = FindCombo(bindings, comboBeingRecorded_)) combo->keys = recorded;
        comboCaptureOverlay_.reset();
        comboBeingRecorded_ = {};
        if (onCombosChanged) onCombosChanged();
        Refresh();
        return;
    }

    // A brand-new combo -- keep the overlay's captured keys alive across
    // the async name prompt, then add + save once named.
    comboCaptureOverlay_.reset();
    auto* prompt = new juce::AlertWindow("Name This Combo", "Recorded: " + DescribeCombo(recorded) + "\n\nName for this combo:",
                                         juce::MessageBoxIconType::QuestionIcon);
    prompt->addTextEditor("name", "NewCombo");
    prompt->addButton("Add", 1);
    prompt->addButton("Cancel", 0);
    auto safeThis = juce::Component::SafePointer<InputBindingsPanel>(this);
    prompt->enterModalState(true, juce::ModalCallbackFunction::create([safeThis, prompt, recorded](int result) mutable {
        std::unique_ptr<juce::AlertWindow> owned(prompt);
        if (result != 1 || safeThis == nullptr) { if (safeThis != nullptr) safeThis->Refresh(); return; }
        const auto name = owned->getTextEditorContents("name").trim();
        if (name.isEmpty()) { safeThis->Refresh(); return; }
        auto& bindingsInner = safeThis->inputActionSystem_.Bindings();
        if (FindCombo(bindingsInner, name) != nullptr) {
            safeThis->statusLabel_.setText("A Combo named \"" + name + "\" already exists.", juce::dontSendNotification);
            safeThis->Refresh();
            return;
        }
        InputCombo combo;
        combo.name = name;
        combo.keys = recorded;
        bindingsInner.combos.add(std::move(combo));
        if (safeThis->onCombosChanged) safeThis->onCombosChanged();
        safeThis->Refresh();
    }), true);
}

void InputBindingsPanel::CancelComboCapture()
{
    comboCaptureOverlay_.reset();
    comboBeingRecorded_ = {};
    Refresh();
}

void InputBindingsPanel::SaveContent()
{
    juce::String error;
    if (!input::InputBindingDocumentStore::save(projectSession_, activeGame_, inputActionSystem_.Bindings(), error) ||
        !projectSession_.commit(error)) {
        statusLabel_.setText("Could not save input bindings: " + error, juce::dontSendNotification);
        return;
    }
    statusLabel_.setText("Saved.", juce::dontSendNotification);
    if (onCombosChanged) onCombosChanged();
}

void InputBindingsPanel::resized()
{
    auto area = getLocalBounds().reduced(8);
    auto headerRow = area.removeFromTop(24);
    saveButton_.setBounds(headerRow.removeFromRight(70).reduced(2));
    addComboButton_.setBounds(headerRow.removeFromRight(90).reduced(2));
    addActionButton_.setBounds(headerRow.removeFromRight(90).reduced(2));
    titleLabel_.setBounds(headerRow);
    area.removeFromTop(4);
    statusLabel_.setBounds(area.removeFromTop(18));
    area.removeFromTop(4);

    for (auto* row : rows_) {
        row->setBounds(area.removeFromTop(row->PreferredHeight()));
        area.removeFromTop(6);
    }

    if (!comboRows_.isEmpty()) {
        area.removeFromTop(4);
        comboSectionLabel_.setBounds(area.removeFromTop(18));
        area.removeFromTop(2);
        for (auto* row : comboRows_) {
            row->setBounds(area.removeFromTop(ComboRow::kRowHeight));
            area.removeFromTop(4);
        }
    }

    if (captureOverlay_ != nullptr) captureOverlay_->setBounds(getLocalBounds());
    if (comboCaptureOverlay_ != nullptr) comboCaptureOverlay_->setBounds(getLocalBounds());
}

void InputBindingsPanel::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff15181d));
}
} // namespace ce::views
