#pragma once

#include <JuceHeader.h>

#include "engine/world.h"

namespace ce {

// SC4: numeric position/rotation/scale editors for whichever entity is
// selected in HierarchyPanel. Reads/writes scene::Transform directly
// against the same World the render thread iterates every frame -- same
// RegistryMutex discipline as HierarchyPanel (see World's own header
// comment for why that lock exists and who else has to use it). Rotation
// is edited in degrees (friendlier than radians) and converted at the
// read/write boundary; storage stays radians to match
// scene::Transform::eulerRotationRadians.
//
// Respects scene::SceneFlags::locked: editors are disabled while the
// selected entity is locked, consistent with "locked means can't be
// changed in the editor" already enforced for hierarchy reparenting.
class TransformPanel final : public juce::Component {
public:
    explicit TransformPanel(engine::World& world);

    void SetSelectedEntity(entt::entity entity);

    // Re-reads the selected entity's Transform/lock state and refreshes
    // the displayed values -- called every timer tick from MainComponent,
    // same as HierarchyPanel::Refresh(). Skips the read while a slider is
    // actively being dragged so a concurrent Refresh() can't fight the
    // user's own in-progress edit.
    void Refresh();

    void resized() override;
    void paint(juce::Graphics& g) override;

    static constexpr int kPreferredHeight = 292;

private:
    void PushToRegistry();
    bool AnySliderBeingDragged() const;
    void SetEditorsVisible(bool visible);

    // AI5: shows Play/Pause for the selected entity's scene::Animator, if
    // it has one -- separate visibility from SetEditorsVisible above,
    // since an entity can have a Transform (editable) without a Skeleton/
    // Animator (nothing to play).
    void TogglePlayback();
    void OnClipSelected();
    void SetAnimationControlsVisible(bool visible);

    engine::World& world_;
    entt::entity selectedEntity_ = entt::null;
    bool locked_ = false;

    juce::Label titleLabel_{ {}, "Transform" };
    juce::Label noSelectionLabel_{ {}, "No entity selected" };

    juce::Label positionLabel_{ {}, "Position (X / Y / Z)" };
    juce::Slider positionXSlider_{ juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::Slider positionYSlider_{ juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::Slider positionZSlider_{ juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };

    juce::Label rotationLabel_{ {}, "Rotation deg (X / Y / Z)" };
    juce::Slider rotationXSlider_{ juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::Slider rotationYSlider_{ juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::Slider rotationZSlider_{ juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };

    juce::Label scaleLabel_{ {}, "Scale (X / Y / Z)" };
    juce::Slider scaleXSlider_{ juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::Slider scaleYSlider_{ juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::Slider scaleZSlider_{ juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };

    // AI5/AI6: only visible when the selected entity has a scene::Animator
    // with at least one clip. clipSelector_ lists every clip on the
    // Animator (AI6's timeline slicing can produce more than one); picking
    // a different one resets playback to that clip's start, paused.
    juce::Label animationLabel_{ {}, "Animation" };
    juce::ComboBox clipSelector_;
    juce::TextButton playPauseButton_{ "Play" };
    juce::StringArray lastShownClipNames_; // avoids rebuilding clipSelector_'s items every 30Hz Refresh() tick.

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TransformPanel)
};

} // namespace ce
