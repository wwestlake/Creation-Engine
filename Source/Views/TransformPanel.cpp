#include "Views/TransformPanel.h"

#include <mutex>

#include "Scene/Components.h"

namespace ce {

namespace {
constexpr int kRowGap = 4;
constexpr int kLabelHeight = 16;
constexpr int kSliderHeight = 22;
} // namespace

TransformPanel::TransformPanel(engine::World& world, interaction::EditorInteraction& interactions)
    : world_(world), interactions_(interactions) {
    titleLabel_.setFont(juce::Font(juce::FontOptions(15.0f)).boldened());
    titleLabel_.setColour(juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible(titleLabel_);

    noSelectionLabel_.setColour(juce::Label::textColourId, juce::Colours::grey);
    addAndMakeVisible(noSelectionLabel_);

    // Mode/space/snap controls: one-shot clicks and typed values, applied
    // directly to EditorInteraction with no locking concerns -- unlike the
    // continuous per-frame gizmo drag this class's siblings deal with, see
    // EditorInteraction.h's own header comment for why that distinction
    // matters here.
    for (auto* button : { &moveModeButton_, &scaleModeButton_, &rotateModeButton_ }) {
        button->setClickingTogglesState(false);
        addAndMakeVisible(button);
    }
    moveModeButton_.onClick = [this] { interactions_.setGizmoMode(interaction::GizmoMode::position); RefreshModeButtons(); };
    scaleModeButton_.onClick = [this] { interactions_.setGizmoMode(interaction::GizmoMode::scale); RefreshModeButtons(); };
    rotateModeButton_.onClick = [this] { interactions_.setGizmoMode(interaction::GizmoMode::rotate); RefreshModeButtons(); };

    spaceToggleButton_.onClick = [this] {
        const bool nowLocal = interactions_.transformSpace() == interaction::TransformSpace::world;
        interactions_.setTransformSpace(nowLocal ? interaction::TransformSpace::local : interaction::TransformSpace::world);
        spaceToggleButton_.setButtonText(nowLocal ? "Local" : "World");
    };
    addAndMakeVisible(spaceToggleButton_);
    RefreshModeButtons();

    gridSnapToggle_.setColour(juce::ToggleButton::textColourId, juce::Colours::lightgrey);
    gridSnapToggle_.setToggleState(interactions_.snapSettings().enabled, juce::dontSendNotification);
    gridSnapToggle_.onClick = [this] { PushSnapSettings(); };
    addAndMakeVisible(gridSnapToggle_);

    gridSnapSlider_.setRange(0.01, 500.0);
    gridSnapSlider_.setValue(interactions_.snapSettings().translationMeters * 100.0, juce::dontSendNotification);
    gridSnapSlider_.onValueChange = [this] { PushSnapSettings(); };
    addAndMakeVisible(gridSnapSlider_);

    angleSnapToggle_.setColour(juce::ToggleButton::textColourId, juce::Colours::lightgrey);
    angleSnapToggle_.setToggleState(interactions_.angleSnapSettings().enabled, juce::dontSendNotification);
    angleSnapToggle_.onClick = [this] { PushSnapSettings(); };
    addAndMakeVisible(angleSnapToggle_);

    angleSnapSlider_.setRange(0.1, 90.0);
    angleSnapSlider_.setValue(interactions_.angleSnapSettings().degrees, juce::dontSendNotification);
    angleSnapSlider_.onValueChange = [this] { PushSnapSettings(); };
    addAndMakeVisible(angleSnapSlider_);

    for (auto* label : { &positionLabel_, &rotationLabel_, &scaleLabel_ }) {
        label->setColour(juce::Label::textColourId, juce::Colours::lightgrey);
        addAndMakeVisible(label);
    }

    for (auto* slider : { &positionXSlider_, &positionYSlider_, &positionZSlider_ }) {
        slider->setRange(-20.0, 20.0);
        slider->onValueChange = [this] { PushToRegistry(); };
        addAndMakeVisible(slider);
    }
    for (auto* slider : { &rotationXSlider_, &rotationYSlider_, &rotationZSlider_ }) {
        slider->setRange(-180.0, 180.0);
        slider->onValueChange = [this] { PushToRegistry(); };
        addAndMakeVisible(slider);
    }
    for (auto* slider : { &scaleXSlider_, &scaleYSlider_, &scaleZSlider_ }) {
        slider->setRange(0.01, 10.0);
        slider->onValueChange = [this] { PushToRegistry(); };
        addAndMakeVisible(slider);
    }

    animationLabel_.setFont(juce::Font(juce::FontOptions(13.0f)).boldened());
    animationLabel_.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    addAndMakeVisible(animationLabel_);

    clipSelector_.onChange = [this] { OnClipSelected(); };
    addAndMakeVisible(clipSelector_);

    playPauseButton_.onClick = [this] { TogglePlayback(); };
    addAndMakeVisible(playPauseButton_);

    behaviorPausedToggle_.setColour(juce::ToggleButton::textColourId, juce::Colours::lightgrey);
    behaviorPausedToggle_.onClick = [this] { ToggleBehaviorPaused(); };
    addAndMakeVisible(behaviorPausedToggle_);

    SetEditorsVisible(false);
    SetAnimationControlsVisible(false);
    SetBehaviorPauseControlsVisible(false);
}

void TransformPanel::SetSelectedEntity(entt::entity entity) {
    selectedEntity_ = entity;
    Refresh();
}

bool TransformPanel::AnySliderBeingDragged() const {
    for (const auto* slider : { &positionXSlider_, &positionYSlider_, &positionZSlider_, &rotationXSlider_,
                                 &rotationYSlider_, &rotationZSlider_, &scaleXSlider_, &scaleYSlider_, &scaleZSlider_ }) {
        if (slider->getThumbBeingDragged() >= 0) {
            return true;
        }
    }
    return false;
}

void TransformPanel::Refresh() {
    if (selectedEntity_ == entt::null) {
        SetEditorsVisible(false);
        SetAnimationControlsVisible(false);
        SetBehaviorPauseControlsVisible(false);
        return;
    }

    if (AnySliderBeingDragged()) {
        return;
    }

    scene::Transform transform;
    bool valid = false;
    {
        std::lock_guard<std::mutex> lock(world_.RegistryMutex());
        auto& registry = world_.Registry();
        if (registry.valid(selectedEntity_) && registry.all_of<scene::Transform>(selectedEntity_)) {
            transform = registry.get<scene::Transform>(selectedEntity_);
            valid = true;
            if (const auto* sceneFlags = registry.try_get<scene::SceneFlags>(selectedEntity_)) {
                locked_ = sceneFlags->locked;
            } else {
                locked_ = false;
            }
        }
    }

    if (!valid) {
        // Selection points at a folder (no Transform) or a since-deleted
        // entity -- either way, nothing here to edit right now.
        selectedEntity_ = entt::null;
        SetEditorsVisible(false);
        SetAnimationControlsVisible(false);
        SetBehaviorPauseControlsVisible(false);
        return;
    }

    positionXSlider_.setValue(transform.position.x, juce::dontSendNotification);
    positionYSlider_.setValue(transform.position.y, juce::dontSendNotification);
    positionZSlider_.setValue(transform.position.z, juce::dontSendNotification);

    rotationXSlider_.setValue(juce::radiansToDegrees(transform.eulerRotationRadians.x), juce::dontSendNotification);
    rotationYSlider_.setValue(juce::radiansToDegrees(transform.eulerRotationRadians.y), juce::dontSendNotification);
    rotationZSlider_.setValue(juce::radiansToDegrees(transform.eulerRotationRadians.z), juce::dontSendNotification);

    scaleXSlider_.setValue(transform.scale.x, juce::dontSendNotification);
    scaleYSlider_.setValue(transform.scale.y, juce::dontSendNotification);
    scaleZSlider_.setValue(transform.scale.z, juce::dontSendNotification);

    SetEditorsVisible(true);

    const bool enabled = !locked_;
    for (auto* slider : { &positionXSlider_, &positionYSlider_, &positionZSlider_, &rotationXSlider_, &rotationYSlider_,
                           &rotationZSlider_, &scaleXSlider_, &scaleYSlider_, &scaleZSlider_ }) {
        slider->setEnabled(enabled);
    }

    bool hasAnimator = false;
    bool playing = false;
    int activeClip = -1;
    juce::StringArray clipNames;
    bool hasBehaviors = false;
    bool behaviorPaused = false;
    {
        std::lock_guard<std::mutex> lock(world_.RegistryMutex());
        auto& registry = world_.Registry();
        if (const auto* animator = registry.try_get<scene::Animator>(selectedEntity_)) {
            if (animator->clips != nullptr && !animator->clips->empty()) {
                hasAnimator = true;
                playing = animator->playing;
                activeClip = animator->activeClip;
                for (const auto& clip : *animator->clips) {
                    clipNames.add(clip.name);
                }
            }
        }
        if (const auto* attachments = registry.try_get<scene::BehaviorAttachments>(selectedEntity_)) {
            hasBehaviors = !attachments->podIds.empty();
        }
        behaviorPaused = registry.all_of<scene::BehaviorPaused>(selectedEntity_);
    }

    SetAnimationControlsVisible(hasAnimator);
    SetBehaviorPauseControlsVisible(hasBehaviors);
    if (hasBehaviors) {
        behaviorPausedToggle_.setToggleState(behaviorPaused, juce::dontSendNotification);
        behaviorPausedToggle_.setEnabled(!locked_);
    }
    if (hasAnimator) {
        if (clipNames != lastShownClipNames_) {
            clipSelector_.clear(juce::dontSendNotification);
            for (int i = 0; i < clipNames.size(); ++i) {
                clipSelector_.addItem(clipNames[i], i + 1);
            }
            lastShownClipNames_ = clipNames;
        }
        clipSelector_.setSelectedId(activeClip + 1, juce::dontSendNotification);
        clipSelector_.setEnabled(!locked_);
        playPauseButton_.setButtonText(playing ? "Pause" : "Play");
        playPauseButton_.setEnabled(!locked_);
    }
}

void TransformPanel::RefreshModeButtons() {
    const auto mode = interactions_.gizmoMode();
    moveModeButton_.setColour(juce::TextButton::buttonColourId,
                              mode == interaction::GizmoMode::position ? juce::Colour(0xff3a6ea8) : juce::Colour(0xff2a2f3a));
    scaleModeButton_.setColour(juce::TextButton::buttonColourId,
                               mode == interaction::GizmoMode::scale ? juce::Colour(0xff3a6ea8) : juce::Colour(0xff2a2f3a));
    rotateModeButton_.setColour(juce::TextButton::buttonColourId,
                                mode == interaction::GizmoMode::rotate ? juce::Colour(0xff3a6ea8) : juce::Colour(0xff2a2f3a));
    // Local/world space only means anything for the move gizmo (see
    // EditorInteraction.h's beginTranslation comment) -- disabled rather
    // than hidden elsewhere so its state doesn't look like it silently
    // reset when you switch modes and back.
    spaceToggleButton_.setEnabled(mode == interaction::GizmoMode::position);
}

void TransformPanel::PushSnapSettings() {
    auto& snap = interactions_.snapSettings();
    snap.enabled = gridSnapToggle_.getToggleState();
    snap.translationMeters = static_cast<float>(gridSnapSlider_.getValue()) / 100.0f;

    auto& angleSnap = interactions_.angleSnapSettings();
    angleSnap.enabled = angleSnapToggle_.getToggleState();
    angleSnap.degrees = static_cast<float>(angleSnapSlider_.getValue());
}

void TransformPanel::PushToRegistry() {
    if (selectedEntity_ == entt::null || locked_) {
        return;
    }

    std::lock_guard<std::mutex> lock(world_.RegistryMutex());
    auto& registry = world_.Registry();
    if (!registry.valid(selectedEntity_) || !registry.all_of<scene::Transform>(selectedEntity_)) {
        return;
    }

    auto& transform = registry.get<scene::Transform>(selectedEntity_);
    transform.position = { static_cast<float>(positionXSlider_.getValue()), static_cast<float>(positionYSlider_.getValue()),
                            static_cast<float>(positionZSlider_.getValue()) };
    transform.eulerRotationRadians = { juce::degreesToRadians(static_cast<float>(rotationXSlider_.getValue())),
                                        juce::degreesToRadians(static_cast<float>(rotationYSlider_.getValue())),
                                        juce::degreesToRadians(static_cast<float>(rotationZSlider_.getValue())) };
    transform.scale = { static_cast<float>(scaleXSlider_.getValue()), static_cast<float>(scaleYSlider_.getValue()),
                         static_cast<float>(scaleZSlider_.getValue()) };
}

void TransformPanel::TogglePlayback() {
    if (selectedEntity_ == entt::null || locked_) {
        return;
    }

    std::lock_guard<std::mutex> lock(world_.RegistryMutex());
    auto& registry = world_.Registry();
    if (!registry.valid(selectedEntity_)) {
        return;
    }

    auto* animator = registry.try_get<scene::Animator>(selectedEntity_);
    if (animator == nullptr || animator->clips == nullptr || animator->activeClip < 0 ||
        static_cast<std::size_t>(animator->activeClip) >= animator->clips->size()) {
        return;
    }

    // Restarting a finished non-looping clip from Play should replay from
    // the top, not do nothing -- ViewportComponent's per-frame update
    // already clamps time to duration and sets playing=false when a
    // non-looping clip runs out, so "at the end, not playing" is exactly
    // the state this button needs to recognize and reset.
    const auto& clip = (*animator->clips)[static_cast<std::size_t>(animator->activeClip)];
    if (!animator->playing && !animator->loop && clip.duration > 0.0f && animator->time >= clip.duration) {
        animator->time = 0.0f;
    }
    animator->playing = !animator->playing;
}

void TransformPanel::OnClipSelected() {
    if (selectedEntity_ == entt::null || locked_) {
        return;
    }

    std::lock_guard<std::mutex> lock(world_.RegistryMutex());
    auto& registry = world_.Registry();
    if (!registry.valid(selectedEntity_)) {
        return;
    }

    auto* animator = registry.try_get<scene::Animator>(selectedEntity_);
    if (animator == nullptr || animator->clips == nullptr) {
        return;
    }

    const int newClip = clipSelector_.getSelectedId() - 1;
    if (newClip < 0 || static_cast<std::size_t>(newClip) >= animator->clips->size()) {
        return;
    }

    // Switching clips restarts from the top, paused -- picking a
    // different clip mid-playback and having it silently keep playing
    // from whatever `time` the old clip happened to be at would be
    // confusing (and could be entirely out of range for the new clip's
    // duration).
    animator->activeClip = newClip;
    animator->time = 0.0f;
    animator->playing = false;
}

void TransformPanel::SetAnimationControlsVisible(bool visible) {
    animationLabel_.setVisible(visible);
    clipSelector_.setVisible(visible);
    playPauseButton_.setVisible(visible);
}

void TransformPanel::ToggleBehaviorPaused() {
    if (selectedEntity_ == entt::null || locked_) {
        return;
    }

    std::lock_guard<std::mutex> lock(world_.RegistryMutex());
    auto& registry = world_.Registry();
    if (!registry.valid(selectedEntity_)) {
        return;
    }

    if (behaviorPausedToggle_.getToggleState()) {
        registry.emplace_or_replace<scene::BehaviorPaused>(selectedEntity_);
    } else {
        registry.remove<scene::BehaviorPaused>(selectedEntity_);
    }
}

void TransformPanel::SetBehaviorPauseControlsVisible(bool visible) {
    behaviorPausedToggle_.setVisible(visible);
}

void TransformPanel::SetEditorsVisible(bool visible) {
    noSelectionLabel_.setVisible(!visible);
    for (juce::Component* component : std::initializer_list<juce::Component*>{
             &positionLabel_, &positionXSlider_, &positionYSlider_, &positionZSlider_, &rotationLabel_, &rotationXSlider_,
             &rotationYSlider_, &rotationZSlider_, &scaleLabel_, &scaleXSlider_, &scaleYSlider_, &scaleZSlider_ }) {
        component->setVisible(visible);
    }
}

void TransformPanel::paint(juce::Graphics&) {}

void TransformPanel::resized() {
    auto bounds = getLocalBounds();

    titleLabel_.setBounds(bounds.removeFromTop(20));

    auto modeRow = bounds.removeFromTop(kSliderHeight + 4);
    const int modeButtonWidth = modeRow.getWidth() * 3 / 8;
    moveModeButton_.setBounds(modeRow.removeFromLeft(modeButtonWidth));
    scaleModeButton_.setBounds(modeRow.removeFromLeft(modeButtonWidth));
    rotateModeButton_.setBounds(modeRow.removeFromLeft(modeButtonWidth));
    spaceToggleButton_.setBounds(modeRow);
    bounds.removeFromTop(kRowGap);

    auto gridSnapRow = bounds.removeFromTop(kSliderHeight);
    gridSnapToggle_.setBounds(gridSnapRow.removeFromLeft(gridSnapRow.getWidth() / 2));
    gridSnapSlider_.setBounds(gridSnapRow);
    bounds.removeFromTop(kRowGap);

    auto angleSnapRow = bounds.removeFromTop(kSliderHeight);
    angleSnapToggle_.setBounds(angleSnapRow.removeFromLeft(angleSnapRow.getWidth() / 2));
    angleSnapSlider_.setBounds(angleSnapRow);
    bounds.removeFromTop(kRowGap);

    noSelectionLabel_.setBounds(bounds.removeFromTop(kLabelHeight));

    positionLabel_.setBounds(bounds.removeFromTop(kLabelHeight));
    auto positionRow = bounds.removeFromTop(kSliderHeight);
    int third = positionRow.getWidth() / 3;
    positionXSlider_.setBounds(positionRow.removeFromLeft(third));
    positionYSlider_.setBounds(positionRow.removeFromLeft(third));
    positionZSlider_.setBounds(positionRow);
    bounds.removeFromTop(kRowGap);

    rotationLabel_.setBounds(bounds.removeFromTop(kLabelHeight));
    auto rotationRow = bounds.removeFromTop(kSliderHeight);
    third = rotationRow.getWidth() / 3;
    rotationXSlider_.setBounds(rotationRow.removeFromLeft(third));
    rotationYSlider_.setBounds(rotationRow.removeFromLeft(third));
    rotationZSlider_.setBounds(rotationRow);
    bounds.removeFromTop(kRowGap);

    scaleLabel_.setBounds(bounds.removeFromTop(kLabelHeight));
    auto scaleRow = bounds.removeFromTop(kSliderHeight);
    third = scaleRow.getWidth() / 3;
    scaleXSlider_.setBounds(scaleRow.removeFromLeft(third));
    scaleYSlider_.setBounds(scaleRow.removeFromLeft(third));
    scaleZSlider_.setBounds(scaleRow);
    bounds.removeFromTop(kRowGap);

    animationLabel_.setBounds(bounds.removeFromTop(kLabelHeight));
    auto animationRow = bounds.removeFromTop(kSliderHeight + 4);
    clipSelector_.setBounds(animationRow.removeFromLeft(animationRow.getWidth() * 2 / 3));
    playPauseButton_.setBounds(animationRow);
    bounds.removeFromTop(kRowGap);

    behaviorPausedToggle_.setBounds(bounds.removeFromTop(kSliderHeight));
}

} // namespace ce
