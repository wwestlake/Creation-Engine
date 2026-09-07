#include "Views/PhysicsPropertiesPanel.h"

#include <mutex>

#include "Physics/PhysicsComponents.h"
#include "Scene/Components.h"

namespace ce {

namespace {
constexpr int kRowGap = 4;
constexpr int kLabelHeight = 16;
constexpr int kSliderHeight = 22;
constexpr int kComboHeight = 24;
constexpr int kButtonHeight = 26;

// ComboBox item ids are 1-based (0 means "nothing selected").
int MotionTypeToItemId(physics::MotionType motionType) { return static_cast<int>(motionType) + 1; }
physics::MotionType ItemIdToMotionType(int itemId) { return static_cast<physics::MotionType>(itemId - 1); }
int ShapeKindToItemId(physics::ColliderShapeKind shape) { return static_cast<int>(shape) + 1; }
physics::ColliderShapeKind ItemIdToShapeKind(int itemId) { return static_cast<physics::ColliderShapeKind>(itemId - 1); }
} // namespace

PhysicsPropertiesPanel::PhysicsPropertiesPanel(engine::World& world) : world_(world) {
    titleLabel_.setFont(juce::Font(juce::FontOptions(15.0f)).boldened());
    titleLabel_.setColour(juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible(titleLabel_);

    noSelectionLabel_.setColour(juce::Label::textColourId, juce::Colours::grey);
    addAndMakeVisible(noSelectionLabel_);

    addPhysicsButton_.onClick = [this] { AddPhysics(); };
    addAndMakeVisible(addPhysicsButton_);
    removePhysicsButton_.onClick = [this] { RemovePhysics(); };
    addAndMakeVisible(removePhysicsButton_);

    motionTypeLabel_.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    addAndMakeVisible(motionTypeLabel_);
    motionTypeCombo_.addItem("Static", MotionTypeToItemId(physics::MotionType::Static));
    motionTypeCombo_.addItem("Kinematic", MotionTypeToItemId(physics::MotionType::Kinematic));
    motionTypeCombo_.addItem("Dynamic", MotionTypeToItemId(physics::MotionType::Dynamic));
    motionTypeCombo_.onChange = [this] { PushToRegistry(); };
    addAndMakeVisible(motionTypeCombo_);

    massLabel_.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    addAndMakeVisible(massLabel_);
    massSlider_.setRange(0.01, 1000.0);
    massSlider_.onValueChange = [this] { PushToRegistry(); };
    addAndMakeVisible(massSlider_);

    frictionLabel_.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    addAndMakeVisible(frictionLabel_);
    frictionSlider_.setRange(0.0, 1.0);
    frictionSlider_.onValueChange = [this] { PushToRegistry(); };
    addAndMakeVisible(frictionSlider_);

    restitutionLabel_.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    addAndMakeVisible(restitutionLabel_);
    restitutionSlider_.setRange(0.0, 1.0);
    restitutionSlider_.onValueChange = [this] { PushToRegistry(); };
    addAndMakeVisible(restitutionSlider_);

    dampingLabel_.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    addAndMakeVisible(dampingLabel_);
    for (auto* slider : { &linearDampingSlider_, &angularDampingSlider_ }) {
        slider->setRange(0.0, 1.0);
        slider->onValueChange = [this] { PushToRegistry(); };
        addAndMakeVisible(slider);
    }

    shapeLabel_.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    addAndMakeVisible(shapeLabel_);
    shapeCombo_.addItem("Box", ShapeKindToItemId(physics::ColliderShapeKind::Box));
    shapeCombo_.addItem("Sphere", ShapeKindToItemId(physics::ColliderShapeKind::Sphere));
    shapeCombo_.addItem("Capsule", ShapeKindToItemId(physics::ColliderShapeKind::Capsule));
    shapeCombo_.onChange = [this] {
        UpdateShapeFieldVisibility();
        PushToRegistry();
    };
    addAndMakeVisible(shapeCombo_);

    halfExtentLabel_.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    addAndMakeVisible(halfExtentLabel_);
    for (auto* slider : { &halfExtentXSlider_, &halfExtentYSlider_, &halfExtentZSlider_ }) {
        slider->setRange(0.01, 50.0);
        slider->onValueChange = [this] { PushToRegistry(); };
        addAndMakeVisible(slider);
    }

    radiusLabel_.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    addAndMakeVisible(radiusLabel_);
    radiusSlider_.setRange(0.01, 50.0);
    radiusSlider_.onValueChange = [this] { PushToRegistry(); };
    addAndMakeVisible(radiusSlider_);

    halfHeightLabel_.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    addAndMakeVisible(halfHeightLabel_);
    halfHeightSlider_.setRange(0.01, 50.0);
    halfHeightSlider_.onValueChange = [this] { PushToRegistry(); };
    addAndMakeVisible(halfHeightSlider_);

    isSensorToggle_.onClick = [this] { PushToRegistry(); };
    addAndMakeVisible(isSensorToggle_);

    SetNoSelectionState(true);
}

void PhysicsPropertiesPanel::SetSelectedEntity(entt::entity entity) {
    selectedEntity_ = entity;
    Refresh();
}

bool PhysicsPropertiesPanel::AnySliderBeingDragged() const {
    for (const auto* slider :
         { &massSlider_, &frictionSlider_, &restitutionSlider_, &linearDampingSlider_, &angularDampingSlider_,
           &halfExtentXSlider_, &halfExtentYSlider_, &halfExtentZSlider_, &radiusSlider_, &halfHeightSlider_ }) {
        if (slider->getThumbBeingDragged() >= 0) {
            return true;
        }
    }
    return false;
}

void PhysicsPropertiesPanel::Refresh() {
    if (selectedEntity_ == entt::null) {
        SetNoSelectionState(true);
        return;
    }

    if (AnySliderBeingDragged()) {
        return;
    }

    physics::RigidBodyComponent rigidBody;
    physics::ColliderComponent collider;
    bool hasPhysics = false;
    {
        std::lock_guard<std::mutex> lock(world_.RegistryMutex());
        auto& registry = world_.Registry();
        if (!registry.valid(selectedEntity_)) {
            selectedEntity_ = entt::null;
            SetNoSelectionState(true);
            return;
        }
        if (const auto* sceneFlags = registry.try_get<scene::SceneFlags>(selectedEntity_)) {
            locked_ = sceneFlags->locked;
        } else {
            locked_ = false;
        }
        hasPhysics = registry.all_of<physics::RigidBodyComponent, physics::ColliderComponent>(selectedEntity_);
        if (hasPhysics) {
            rigidBody = registry.get<physics::RigidBodyComponent>(selectedEntity_);
            collider = registry.get<physics::ColliderComponent>(selectedEntity_);
        }
    }

    SetNoSelectionState(false);
    SetHasPhysicsState(hasPhysics);
    if (!hasPhysics) {
        return;
    }

    motionTypeCombo_.setSelectedId(MotionTypeToItemId(rigidBody.motionType), juce::dontSendNotification);
    massSlider_.setValue(rigidBody.mass, juce::dontSendNotification);
    frictionSlider_.setValue(rigidBody.friction, juce::dontSendNotification);
    restitutionSlider_.setValue(rigidBody.restitution, juce::dontSendNotification);
    linearDampingSlider_.setValue(rigidBody.linearDamping, juce::dontSendNotification);
    angularDampingSlider_.setValue(rigidBody.angularDamping, juce::dontSendNotification);

    shapeCombo_.setSelectedId(ShapeKindToItemId(collider.shape), juce::dontSendNotification);
    halfExtentXSlider_.setValue(collider.halfExtentX, juce::dontSendNotification);
    halfExtentYSlider_.setValue(collider.halfExtentY, juce::dontSendNotification);
    halfExtentZSlider_.setValue(collider.halfExtentZ, juce::dontSendNotification);
    radiusSlider_.setValue(collider.radius, juce::dontSendNotification);
    halfHeightSlider_.setValue(collider.halfHeight, juce::dontSendNotification);
    isSensorToggle_.setToggleState(collider.isSensor, juce::dontSendNotification);
    UpdateShapeFieldVisibility();

    const bool enabled = !locked_;
    for (juce::Component* control : std::initializer_list<juce::Component*>{
             &motionTypeCombo_, &massSlider_, &frictionSlider_, &restitutionSlider_, &linearDampingSlider_,
             &angularDampingSlider_, &shapeCombo_, &halfExtentXSlider_, &halfExtentYSlider_, &halfExtentZSlider_,
             &radiusSlider_, &halfHeightSlider_, &isSensorToggle_, &removePhysicsButton_ }) {
        control->setEnabled(enabled);
    }
}

void PhysicsPropertiesPanel::PushToRegistry() {
    if (selectedEntity_ == entt::null || locked_ || !hasPhysics_) {
        return;
    }

    std::lock_guard<std::mutex> lock(world_.RegistryMutex());
    auto& registry = world_.Registry();
    if (!registry.valid(selectedEntity_) ||
        !registry.all_of<physics::RigidBodyComponent, physics::ColliderComponent>(selectedEntity_)) {
        return;
    }

    auto& rigidBody = registry.get<physics::RigidBodyComponent>(selectedEntity_);
    rigidBody.motionType = ItemIdToMotionType(motionTypeCombo_.getSelectedId());
    rigidBody.mass = static_cast<float>(massSlider_.getValue());
    rigidBody.friction = static_cast<float>(frictionSlider_.getValue());
    rigidBody.restitution = static_cast<float>(restitutionSlider_.getValue());
    rigidBody.linearDamping = static_cast<float>(linearDampingSlider_.getValue());
    rigidBody.angularDamping = static_cast<float>(angularDampingSlider_.getValue());

    auto& collider = registry.get<physics::ColliderComponent>(selectedEntity_);
    collider.shape = ItemIdToShapeKind(shapeCombo_.getSelectedId());
    collider.halfExtentX = static_cast<float>(halfExtentXSlider_.getValue());
    collider.halfExtentY = static_cast<float>(halfExtentYSlider_.getValue());
    collider.halfExtentZ = static_cast<float>(halfExtentZSlider_.getValue());
    collider.radius = static_cast<float>(radiusSlider_.getValue());
    collider.halfHeight = static_cast<float>(halfHeightSlider_.getValue());
    collider.isSensor = isSensorToggle_.getToggleState();
}

void PhysicsPropertiesPanel::AddPhysics() {
    if (selectedEntity_ == entt::null || locked_) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(world_.RegistryMutex());
        auto& registry = world_.Registry();
        if (!registry.valid(selectedEntity_)) {
            return;
        }
        registry.emplace_or_replace<physics::RigidBodyComponent>(selectedEntity_);
        registry.emplace_or_replace<physics::ColliderComponent>(selectedEntity_);
    }
    Refresh();
}

void PhysicsPropertiesPanel::RemovePhysics() {
    if (selectedEntity_ == entt::null || locked_) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(world_.RegistryMutex());
        auto& registry = world_.Registry();
        if (!registry.valid(selectedEntity_)) {
            return;
        }
        registry.remove<physics::RigidBodyComponent>(selectedEntity_);
        registry.remove<physics::ColliderComponent>(selectedEntity_);
    }
    Refresh();
}

void PhysicsPropertiesPanel::SetNoSelectionState(bool noSelection) {
    noSelectionLabel_.setVisible(noSelection);
    addPhysicsButton_.setVisible(false);
    if (noSelection) {
        SetHasPhysicsState(false);
    }
}

void PhysicsPropertiesPanel::SetHasPhysicsState(bool hasPhysics) {
    hasPhysics_ = hasPhysics;
    addPhysicsButton_.setVisible(!noSelectionLabel_.isVisible() && !hasPhysics);
    removePhysicsButton_.setVisible(!noSelectionLabel_.isVisible() && hasPhysics);
    for (juce::Component* control : std::initializer_list<juce::Component*>{
             &motionTypeLabel_, &motionTypeCombo_, &massLabel_, &massSlider_, &frictionLabel_, &frictionSlider_,
             &restitutionLabel_, &restitutionSlider_, &dampingLabel_, &linearDampingSlider_, &angularDampingSlider_,
             &shapeLabel_, &shapeCombo_, &halfExtentLabel_, &halfExtentXSlider_, &halfExtentYSlider_,
             &halfExtentZSlider_, &radiusLabel_, &radiusSlider_, &halfHeightLabel_, &halfHeightSlider_,
             &isSensorToggle_ }) {
        control->setVisible(hasPhysics);
    }
    if (hasPhysics) {
        UpdateShapeFieldVisibility();
    }
    resized();
}

void PhysicsPropertiesPanel::UpdateShapeFieldVisibility() {
    const auto shape = ItemIdToShapeKind(shapeCombo_.getSelectedId());
    const bool isBox = shape == physics::ColliderShapeKind::Box;
    const bool isSphere = shape == physics::ColliderShapeKind::Sphere;
    const bool isCapsule = shape == physics::ColliderShapeKind::Capsule;

    halfExtentLabel_.setVisible(isBox);
    halfExtentXSlider_.setVisible(isBox);
    halfExtentYSlider_.setVisible(isBox);
    halfExtentZSlider_.setVisible(isBox);

    radiusLabel_.setVisible(isSphere || isCapsule);
    radiusSlider_.setVisible(isSphere || isCapsule);

    halfHeightLabel_.setVisible(isCapsule);
    halfHeightSlider_.setVisible(isCapsule);

    resized();
}

void PhysicsPropertiesPanel::paint(juce::Graphics&) {}

int PhysicsPropertiesPanel::PreferredHeight() const {
    if (noSelectionLabel_.isVisible()) {
        return 20 + kRowGap + kLabelHeight;
    }
    if (!hasPhysics_) {
        return 20 + kRowGap + kButtonHeight;
    }

    const auto shape = ItemIdToShapeKind(shapeCombo_.getSelectedId());
    int shapeFieldHeight = 0;
    if (shape == physics::ColliderShapeKind::Box) {
        shapeFieldHeight = kLabelHeight + kSliderHeight;
    } else if (shape == physics::ColliderShapeKind::Sphere) {
        shapeFieldHeight = kLabelHeight + kSliderHeight;
    } else {
        shapeFieldHeight = (kLabelHeight + kSliderHeight) + kRowGap + (kLabelHeight + kSliderHeight);
    }

    return 20                                              // title
         + (kLabelHeight + kComboHeight) + kRowGap          // motion type
         + (kLabelHeight + kSliderHeight) + kRowGap         // mass
         + (kLabelHeight + kSliderHeight) + kRowGap         // friction
         + (kLabelHeight + kSliderHeight) + kRowGap         // restitution
         + (kLabelHeight + kSliderHeight) + kRowGap         // damping
         + (kLabelHeight + kComboHeight) + kRowGap          // shape
         + shapeFieldHeight + kRowGap                       // shape-specific fields
         + kSliderHeight + kRowGap                          // sensor toggle
         + kButtonHeight;                                   // remove button
}

void PhysicsPropertiesPanel::resized() {
    auto bounds = getLocalBounds();

    titleLabel_.setBounds(bounds.removeFromTop(20));

    if (noSelectionLabel_.isVisible()) {
        noSelectionLabel_.setBounds(bounds.removeFromTop(kLabelHeight));
        return;
    }
    if (!hasPhysics_) {
        addPhysicsButton_.setBounds(bounds.removeFromTop(kButtonHeight));
        return;
    }

    motionTypeLabel_.setBounds(bounds.removeFromTop(kLabelHeight));
    motionTypeCombo_.setBounds(bounds.removeFromTop(kComboHeight));
    bounds.removeFromTop(kRowGap);

    massLabel_.setBounds(bounds.removeFromTop(kLabelHeight));
    massSlider_.setBounds(bounds.removeFromTop(kSliderHeight));
    bounds.removeFromTop(kRowGap);

    frictionLabel_.setBounds(bounds.removeFromTop(kLabelHeight));
    frictionSlider_.setBounds(bounds.removeFromTop(kSliderHeight));
    bounds.removeFromTop(kRowGap);

    restitutionLabel_.setBounds(bounds.removeFromTop(kLabelHeight));
    restitutionSlider_.setBounds(bounds.removeFromTop(kSliderHeight));
    bounds.removeFromTop(kRowGap);

    dampingLabel_.setBounds(bounds.removeFromTop(kLabelHeight));
    auto dampingRow = bounds.removeFromTop(kSliderHeight);
    const int half = dampingRow.getWidth() / 2;
    linearDampingSlider_.setBounds(dampingRow.removeFromLeft(half));
    angularDampingSlider_.setBounds(dampingRow);
    bounds.removeFromTop(kRowGap);

    shapeLabel_.setBounds(bounds.removeFromTop(kLabelHeight));
    shapeCombo_.setBounds(bounds.removeFromTop(kComboHeight));
    bounds.removeFromTop(kRowGap);

    if (halfExtentLabel_.isVisible()) {
        halfExtentLabel_.setBounds(bounds.removeFromTop(kLabelHeight));
        auto extentRow = bounds.removeFromTop(kSliderHeight);
        const int third = extentRow.getWidth() / 3;
        halfExtentXSlider_.setBounds(extentRow.removeFromLeft(third));
        halfExtentYSlider_.setBounds(extentRow.removeFromLeft(third));
        halfExtentZSlider_.setBounds(extentRow);
        bounds.removeFromTop(kRowGap);
    }
    if (radiusLabel_.isVisible()) {
        radiusLabel_.setBounds(bounds.removeFromTop(kLabelHeight));
        radiusSlider_.setBounds(bounds.removeFromTop(kSliderHeight));
        bounds.removeFromTop(kRowGap);
    }
    if (halfHeightLabel_.isVisible()) {
        halfHeightLabel_.setBounds(bounds.removeFromTop(kLabelHeight));
        halfHeightSlider_.setBounds(bounds.removeFromTop(kSliderHeight));
        bounds.removeFromTop(kRowGap);
    }

    isSensorToggle_.setBounds(bounds.removeFromTop(kSliderHeight));
    bounds.removeFromTop(kRowGap);

    removePhysicsButton_.setBounds(bounds.removeFromTop(kButtonHeight));
}

} // namespace ce
