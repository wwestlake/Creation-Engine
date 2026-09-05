#pragma once

#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

// Direct module headers, not <JuceHeader.h> -- this file is compiled
// directly into lean test targets (CreationEngineFrustPluginSmoke,
// CreationEngineObjectBehaviorSmoke) that have no generated JuceHeader.h
// of their own, same reason EngineFrustHost.h/.cpp already avoid it.
// juce_gui_basics specifically for KeyPress/ModifierKeys (PollOncePerFrame
// in the .cpp) -- juce_core alone (what those targets link today) doesn't
// have them.
#include <juce_core/juce_core.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <creation/assets/ProjectSession.h>

#include "Input/InputBindingTypes.h"
#include "Project/EngineGameDocument.h"

namespace ce::input
{
// Seam for a controller backend, deliberately decoupled from the binding
// data model/editor UI/FRust nodes -- Input Binding System plan's own
// controller-backend decision (structural-only for now): NullControllerBackend
// ships today, always reporting disconnected. A real backend (XInput,
// most likely, being the lowest-cost real option for the most common
// controller type) is one new subclass implementing this interface;
// nothing else in this system changes when one is added later.
class InputControllerBackend
{
public:
    virtual ~InputControllerBackend() = default;
    virtual bool IsConnected() const = 0;
    virtual bool Button(int index) const = 0;
    virtual int AxisPerMille(int index) const = 0; // -1000..1000 (or 0..1000 for a trigger-style axis).
};

class NullControllerBackend final : public InputControllerBackend
{
public:
    bool IsConnected() const override { return false; }
    bool Button(int) const override { return false; }
    int AxisPerMille(int) const override { return 0; }
};

// One raw key-down edge, timestamped in milliseconds (wall-clock,
// juce::Time::getMillisecondCounterHiRes() -- not an assumed fixed tick
// duration, so detection stays correct regardless of actual frame timing
// jitter). Oldest-first order, matching recording/playback order.
using RawKeyEvent = std::pair<int, double>;

// True if the last combo.keys.size() entries of `recentKeyDownEvents`
// match combo.keys in order (same keycodes, same sequence) AND each
// consecutive timestamp delta is within tolerance of the recorded
// offsetMillis. A chord (near-zero recorded offsets) and a short leader-
// key sequence (larger recorded offsets) are both just this one matcher
// applied to different recorded data -- no separate "mode." Exposed as a
// free function (not a class member) specifically so it's directly
// testable without driving real OS key state.
bool MatchesCombo(const std::vector<RawKeyEvent>& recentKeyDownEvents, const InputCombo& combo);

// Polls raw OS input state once per simulation tick and evaluates the
// active Game's Action bindings against it -- follows FreeCamera.cpp's own
// established precedent exactly (juce::KeyPress::isKeyCurrentlyDown /
// juce::ModifierKeys::getCurrentModifiersRealtime(), polled fresh every
// call, never JUCE's routed keyPressed/mouseDown) because a Pod's on_tick
// needs a live level/edge every tick, the same continuous-state need
// FreeCamera already solved this way.
class InputActionSystem final
{
public:
    explicit InputActionSystem(std::unique_ptr<InputControllerBackend> controllerBackend = std::make_unique<NullControllerBackend>());

    // Replaces the loaded bindings wholesale -- called from MainComponent
    // whenever activeGame_ changes (openActiveGame, selectGame).
    void LoadForGame(creation::assets::ProjectSession& session,
                     const project::GameDocumentInfo& game,
                     juce::String& errorMessage);

    // Called once per simulation tick (MainComponent::timerCallback, first
    // line inside `if (isPlaying_)`, before Simulation::Step/
    // FoundationGameplay::Step/frustHost_.tick()) so a Pod's on_tick this
    // same tick sees this tick's poll, not last tick's.
    void PollOncePerFrame();

    [[nodiscard]] bool IsActionActive(const juce::String& actionName) const;
    [[nodiscard]] bool WasActionPressed(const juce::String& actionName) const;  // edge: false->true this tick.
    [[nodiscard]] bool WasActionReleased(const juce::String& actionName) const; // edge: true->false this tick.
    [[nodiscard]] int GetActionValuePerMille(const juce::String& actionName) const; // Analog only; Digital -> 1000/0.

    // Combos that completed (matched) during the most recent
    // PollOncePerFrame() call -- cleared and rebuilt fresh every call, so
    // it reflects only this tick's completions, not a running history.
    // EngineFrustHost::tick() reads this to fire "on_action_<name>" for
    // every attached Pod (Input Combo Events plan).
    [[nodiscard]] const juce::StringArray& FiredCombos() const { return firedCombos_; }

    // Direct read/write access for InputBindingsPanel, which edits the
    // live set in place; Save persists it via InputBindingDocumentStore,
    // a reload (LoadForGame) discards unsaved edits.
    [[nodiscard]] InputBindingSet& Bindings() { return bindings_; }
    [[nodiscard]] const InputBindingSet& Bindings() const { return bindings_; }

private:
    struct ActionState { bool active = false; int valuePerMille = 0; };

    [[nodiscard]] const ActionState* Find(const std::unordered_map<juce::String, ActionState>& states,
                                          const juce::String& actionName) const;

    InputBindingSet bindings_;
    std::unique_ptr<InputControllerBackend> controllerBackend_;
    std::unordered_map<juce::String, ActionState> current_;
    std::unordered_map<juce::String, ActionState> previous_;

    // Combo detection state -- independent of the Action-evaluation state
    // above. recentKeyDownEvents_ keeps only entries within a generous
    // outer window (kMaxComboWindowMs); previousRawKeysDown_ tracks last
    // poll's raw down-set so PollOncePerFrame can detect new-down edges
    // (a combo keypress must fire once per press, not once per tick a key
    // stays held).
    static constexpr double kMaxComboWindowMs = 2000.0;
    std::vector<RawKeyEvent> recentKeyDownEvents_;
    std::unordered_map<int, bool> previousRawKeysDown_;
    juce::StringArray firedCombos_;
};
} // namespace ce::input
