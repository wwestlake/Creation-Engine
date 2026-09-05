#include "Input/InputActionSystem.h"

#include <cstdlib>

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
