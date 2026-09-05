#include "Input/InputActionSystem.h"

#include <cmath>
#include <cstdlib>
#include <utility>

#include "Input/InputBindingDocumentStore.h"

namespace ce::input
{
namespace
{
// `foreground` only gates KeyboardKey/MouseButton -- those read raw,
// window-focus-scoped OS state (the same reason FreeCamera::Update guards
// its own WASD polling on juce::Process::isForegroundProcess(): without
// it, a key typed into some other app entirely still reads as "down").
// A controller has no such per-window-focus semantics -- a real backend
// (XInput or otherwise) reads a dedicated hardware API scoped to this
// process's own handle, not "whatever window has OS focus" -- so
// ControllerButton/ControllerAxis are deliberately NOT gated here.
bool EvaluateDigitalSource(const InputBinding& binding, const InputControllerBackend& controllerBackend, bool foreground)
{
    switch (binding.sourceKind) {
        case InputSourceKind::KeyboardKey:
            return foreground && juce::KeyPress::isKeyCurrentlyDown(binding.code);
        case InputSourceKind::MouseButton: {
            if (!foreground) return false;
            const auto mods = juce::ModifierKeys::getCurrentModifiersRealtime();
            if (binding.code == 0) return mods.isLeftButtonDown();
            if (binding.code == 1) return mods.isRightButtonDown();
            return mods.isMiddleButtonDown();
        }
        case InputSourceKind::ControllerButton:
            return controllerBackend.Button(binding.code);
        case InputSourceKind::ControllerAxis:
            return std::abs(controllerBackend.AxisPerMille(binding.code)) > 0;
    }
    return false;
}

// The per-mille magnitude `binding` contributes to an Analog action right
// now -- a Digital source (key/mouse/controller button) contributes its
// own authored analogMagnitude while held, 0 while not; a ControllerAxis
// reports its own live magnitude directly.
int EvaluateAnalogContribution(const InputBinding& binding, const InputControllerBackend& controllerBackend, bool foreground)
{
    if (binding.sourceKind == InputSourceKind::ControllerAxis) {
        return std::abs(controllerBackend.AxisPerMille(binding.code));
    }
    return EvaluateDigitalSource(binding, controllerBackend, foreground) ? binding.analogMagnitude : 0;
}
} // namespace

bool MatchesCombo(const std::vector<RawKeyEvent>& recentKeyDownEvents, const InputCombo& combo)
{
    const int n = combo.keys.size();
    if (n <= 0 || static_cast<int>(recentKeyDownEvents.size()) < n) {
        return false;
    }

    // Only the LAST n buffer entries can possibly be this combo -- a
    // combo is always matched against the most recently completed
    // sequence of key-down edges, never a stale one buried earlier.
    const auto start = static_cast<std::size_t>(static_cast<int>(recentKeyDownEvents.size()) - n);
    for (int i = 0; i < n; ++i) {
        if (recentKeyDownEvents[start + static_cast<std::size_t>(i)].first != combo.keys[i].keyCode) {
            return false;
        }
    }

    const double firstTime = recentKeyDownEvents[start].second;
    for (int i = 1; i < n; ++i) {
        const double actualOffset = recentKeyDownEvents[start + static_cast<std::size_t>(i)].second - firstTime;
        const double recordedOffset = static_cast<double>(combo.keys[i].offsetMillis);
        // Generous tolerance: human timing varies a lot more than machine
        // timing -- 150ms floor for near-simultaneous chords, 60% of the
        // recorded gap for longer sequences (a 300ms-recorded pause can
        // reasonably land anywhere from ~120ms to ~480ms and still read
        // as "the same combo" to the person who recorded it).
        const double tolerance = juce::jmax(150.0, recordedOffset * 0.6);
        if (std::abs(actualOffset - recordedOffset) > tolerance) {
            return false;
        }
    }
    return true;
}

InputActionSystem::InputActionSystem(std::unique_ptr<InputControllerBackend> controllerBackend)
    : controllerBackend_(std::move(controllerBackend))
{
}

void InputActionSystem::LoadForGame(creation::assets::ProjectSession& session,
                                     const project::GameDocumentInfo& game,
                                     juce::String& errorMessage)
{
    InputBindingDocumentStore::load(session, game, bindings_, errorMessage);
    current_.clear();
    previous_.clear();
}

void InputActionSystem::PollOncePerFrame()
{
    previous_ = current_;
    current_.clear();

    // See EvaluateDigitalSource's own comment: this only suppresses
    // KeyboardKey/MouseButton bindings, not controller ones.
    const bool foreground = juce::Process::isForegroundProcess();

    for (const auto& action : bindings_.actions) {
        ActionState state;
        if (action.kind == ActionKind::Digital) {
            for (const auto& binding : action.bindings) {
                if (EvaluateDigitalSource(binding, *controllerBackend_, foreground)) {
                    state.active = true;
                    break;
                }
            }
            state.valuePerMille = state.active ? 1000 : 0;
        } else {
            for (const auto& binding : action.bindings) {
                state.valuePerMille = juce::jmax(state.valuePerMille, EvaluateAnalogContribution(binding, *controllerBackend_, foreground));
            }
            state.active = state.valuePerMille > 0;
        }
        current_[action.name] = state;
    }

    // Combo detection -- independent of the Action loop above. Only scans
    // the (typically small) set of keys any defined combo actually cares
    // about, so this stays cheap even with dozens of unrelated Actions
    // bound elsewhere.
    firedCombos_.clear();
    std::unordered_map<int, bool> currentRawKeysDown;
    for (const auto& combo : bindings_.combos) {
        for (const auto& key : combo.keys) {
            if (currentRawKeysDown.contains(key.keyCode)) continue;
            currentRawKeysDown[key.keyCode] = foreground && juce::KeyPress::isKeyCurrentlyDown(key.keyCode);
        }
    }

    const double nowMs = juce::Time::getMillisecondCounterHiRes();
    for (const auto& [code, isDown] : currentRawKeysDown) {
        const auto previousIt = previousRawKeysDown_.find(code);
        const bool wasDown = previousIt != previousRawKeysDown_.end() && previousIt->second;
        if (isDown && !wasDown) {
            recentKeyDownEvents_.emplace_back(code, nowMs);
        }
    }
    previousRawKeysDown_ = std::move(currentRawKeysDown);

    while (!recentKeyDownEvents_.empty() && nowMs - recentKeyDownEvents_.front().second > kMaxComboWindowMs) {
        recentKeyDownEvents_.erase(recentKeyDownEvents_.begin());
    }

    // Longest combo first: a buffer tail of [Escape, G] should satisfy a
    // defined "Escape, G" combo in preference to a shorter "G"-alone
    // combo also matching the same trailing entry. A completed combo
    // resets the whole buffer (accepted simplification -- see
    // InputCombo's own header comment) so at most one combo fires per
    // tick.
    for (int keyCount = 3; keyCount >= 1; --keyCount) {
        bool matched = false;
        for (const auto& combo : bindings_.combos) {
            if (combo.name.isEmpty() || combo.keys.size() != keyCount) continue;
            if (MatchesCombo(recentKeyDownEvents_, combo)) {
                firedCombos_.add(combo.name);
                recentKeyDownEvents_.clear();
                matched = true;
                break;
            }
        }
        if (matched) break;
    }
}

const InputActionSystem::ActionState* InputActionSystem::Find(const std::unordered_map<juce::String, ActionState>& states,
                                                                const juce::String& actionName) const
{
    const auto found = states.find(actionName);
    return found != states.end() ? &found->second : nullptr;
}

bool InputActionSystem::IsActionActive(const juce::String& actionName) const
{
    const auto* state = Find(current_, actionName);
    return state != nullptr && state->active;
}

bool InputActionSystem::WasActionPressed(const juce::String& actionName) const
{
    const auto* now = Find(current_, actionName);
    const auto* before = Find(previous_, actionName);
    const bool wasActive = before != nullptr && before->active;
    const bool isActive = now != nullptr && now->active;
    return isActive && !wasActive;
}

bool InputActionSystem::WasActionReleased(const juce::String& actionName) const
{
    const auto* now = Find(current_, actionName);
    const auto* before = Find(previous_, actionName);
    const bool wasActive = before != nullptr && before->active;
    const bool isActive = now != nullptr && now->active;
    return wasActive && !isActive;
}

int InputActionSystem::GetActionValuePerMille(const juce::String& actionName) const
{
    const auto* state = Find(current_, actionName);
    return state != nullptr ? state->valuePerMille : 0;
}
} // namespace ce::input
