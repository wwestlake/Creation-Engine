#pragma once

#include <juce_core/juce_core.h>

namespace ce::input
{
// What raw OS-level thing a binding polls. ControllerButton/ControllerAxis
// exist as real data shapes now; whether anything actually reports
// non-zero for them depends entirely on which InputControllerBackend
// InputActionSystem is constructed with (see InputActionSystem.h) --
// deliberately decoupled so this data model, the editor panel, and the
// FRust nodes never have to change regardless of that decision.
enum class InputSourceKind
{
    KeyboardKey,
    MouseButton,
    ControllerButton,
    ControllerAxis
};

// Two independent Action kinds. Composite/signed axis mapping (e.g. one
// continuous "MoveForward" driven by W=+1/S=-1, the way old Unity Input
// Manager axes worked) is deliberately not modeled -- a game that wants
// that defines two Actions and does the subtraction in its own Pod's
// FRust logic, which already has arithmetic. Named here as a real,
// deferred future enhancement, not built.
enum class ActionKind
{
    Digital,
    Analog
};

// One raw source contributing to an Action.
//  - code: KeyboardKey -> a JUCE key-code int, the same raw literal
//    FreeCamera.cpp already hardcodes for 'W'/'S'/'A'/'D'/'E'/'Q', just
//    captured as data instead of hardcoded. MouseButton -> 0=Left,
//    1=Right, 2=Middle. ControllerButton/ControllerAxis -> an abstract
//    0-based index into whatever InputControllerBackend is constructed.
//  - analogMagnitude: the per-mille value (1000 = full) this source
//    contributes to an Analog action while active. Meaningless for a
//    Digital action (which only needs on/off) and for ControllerAxis
//    (which reports its own live per-mille magnitude instead).
struct InputBinding
{
    InputSourceKind sourceKind = InputSourceKind::KeyboardKey;
    int code = 0;
    int analogMagnitude = 1000;
};

struct InputAction
{
    juce::String name; // unique within an InputBindingSet, e.g. "Jump", "MoveForward".
    ActionKind kind = ActionKind::Digital;
    juce::Array<InputBinding> bindings; // Digital: OR'd. Analog: MAX magnitude wins.
};

// One InputBindings document == one Game (see InputBindingDocumentStore) --
// not a separately browsable/versioned Thing, same reasoning games.xml
// itself is not one (docs/OBJECT_MODEL.md's create/list/open/save/delete/
// rename test): there is exactly one of these per Game, by design.
struct InputBindingSet
{
    juce::Array<InputAction> actions;
};
} // namespace ce::input
