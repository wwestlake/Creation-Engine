#pragma once

#include <vector>

// Direct module headers, not <JuceHeader.h> -- this header is included
// from tests/InputActionSystemSmoke.cpp, which is compiled into a lean
// test target with no generated JuceHeader.h of its own, same reason
// InputActionSystem.h avoids it. juce_gui_basics specifically for
// KeyPress::spaceKey/upKey/downKey/leftKey/rightKey.
#include <juce_core/juce_core.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "Input/InputBindingTypes.h"

namespace ce::input {

// One choice in the Input Bindings panel's "Load Preset" picker. Plain
// data, no UI dependency -- an open-ended std::vector rather than a
// hardcoded enum/switch, same reasoning as Project::StarterGameTemplate's
// own header comment: a future preset is a one-line addition here, never
// a dialog code change.
//
// Scoped tightly to "basic character movement controls," as asked --
// MoveForward/MoveBackward/MoveLeft/MoveRight/Jump/Sprint/Crouch, all
// Digital Actions, `combos` left empty. No interact/fire/aim/reload; a
// broader gameplay-control preset is a separate, future addition, not
// built speculatively here.
//
// Grounded in actual PC-gaming convention (not asserted from memory --
// see the Modifier-Key Bindings + Starter Movement Mapping Presets plan
// for sourcing): WASD+Space+Shift+Ctrl is the entrenched default since
// the Quake/World-of-Warcraft era; ESDF is a real, well-documented
// home-row-ergonomic alternative (same hand, one column right -- NOT a
// distinct "left-handed" scheme despite how it's sometimes described);
// Arrow Keys were the pre-WASD standard and remain a common alternative
// several games still offer. Sprint/Crouch are hold-while-down (what a
// Digital Action's IsActionActive already means) -- a toggle variant is
// Pod-side logic (WasActionPressed flipping a stored bool), not something
// a preset itself needs to implement.
struct InputMappingPreset {
    juce::String name;
    juce::String description;
    InputBindingSet bindings;
};

namespace detail {
inline InputAction MakeDigitalAction(const juce::String& name, InputBinding binding) {
    InputAction action;
    action.name = name;
    action.kind = ActionKind::Digital;
    action.bindings.add(binding);
    return action;
}
} // namespace detail

inline std::vector<InputMappingPreset> GetStarterInputMappingPresets() {
    using detail::MakeDigitalAction;

    // Sprint/Crouch are identical across all three presets (Shift/Ctrl
    // don't distinguish which hand reaches for movement, so there's no
    // reason to vary them per scheme).
    const InputBinding jump{ InputSourceKind::KeyboardKey, juce::KeyPress::spaceKey, 1000 };
    const InputBinding sprint{ InputSourceKind::ModifierKey, 0 /*Shift*/, 1000 };
    const InputBinding crouch{ InputSourceKind::ModifierKey, 1 /*Ctrl*/, 1000 };

    InputMappingPreset standard;
    standard.name = "Standard (WASD)";
    standard.description = "The entrenched PC-gaming default since the Quake/World of Warcraft era.";
    standard.bindings.actions = {
        MakeDigitalAction("MoveForward", { InputSourceKind::KeyboardKey, 'W', 1000 }),
        MakeDigitalAction("MoveBackward", { InputSourceKind::KeyboardKey, 'S', 1000 }),
        MakeDigitalAction("MoveLeft", { InputSourceKind::KeyboardKey, 'A', 1000 }),
        MakeDigitalAction("MoveRight", { InputSourceKind::KeyboardKey, 'D', 1000 }),
        MakeDigitalAction("Jump", jump),
        MakeDigitalAction("Sprint", sprint),
        MakeDigitalAction("Crouch", crouch),
    };

    InputMappingPreset ergonomic;
    ergonomic.name = "Ergonomic (ESDF)";
    ergonomic.description = "Home-row movement (one column right of WASD) -- a real, well-documented alternative, not a distinct left-handed scheme.";
    ergonomic.bindings.actions = {
        MakeDigitalAction("MoveForward", { InputSourceKind::KeyboardKey, 'E', 1000 }),
        MakeDigitalAction("MoveBackward", { InputSourceKind::KeyboardKey, 'D', 1000 }),
        MakeDigitalAction("MoveLeft", { InputSourceKind::KeyboardKey, 'S', 1000 }),
        MakeDigitalAction("MoveRight", { InputSourceKind::KeyboardKey, 'F', 1000 }),
        MakeDigitalAction("Jump", jump),
        MakeDigitalAction("Sprint", sprint),
        MakeDigitalAction("Crouch", crouch),
    };

    InputMappingPreset arrowKeys;
    arrowKeys.name = "Arrow Keys";
    arrowKeys.description = "The pre-WASD standard, still offered as an alternative in many games' settings.";
    arrowKeys.bindings.actions = {
        MakeDigitalAction("MoveForward", { InputSourceKind::KeyboardKey, juce::KeyPress::upKey, 1000 }),
        MakeDigitalAction("MoveBackward", { InputSourceKind::KeyboardKey, juce::KeyPress::downKey, 1000 }),
        MakeDigitalAction("MoveLeft", { InputSourceKind::KeyboardKey, juce::KeyPress::leftKey, 1000 }),
        MakeDigitalAction("MoveRight", { InputSourceKind::KeyboardKey, juce::KeyPress::rightKey, 1000 }),
        MakeDigitalAction("Jump", jump),
        MakeDigitalAction("Sprint", sprint),
        MakeDigitalAction("Crouch", crouch),
    };

    return { standard, ergonomic, arrowKeys };
}

} // namespace ce::input
