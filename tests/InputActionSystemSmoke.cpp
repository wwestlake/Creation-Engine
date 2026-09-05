#include "Input/InputActionSystem.h"

#include "node_system/core_control_flow.h"

#include <iostream>
#include <memory>

namespace
{
// Test double: reports whatever axisValue is set to for axis 0, ignores
// every button. Lets the Analog-action path (ControllerAxis contributing
// its own live magnitude) be exercised with no real controller hardware,
// same reasoning the Input Binding System plan's own controller-backend
// decision (structural-only for now) is built around.
class FakeControllerBackend final : public ce::input::InputControllerBackend
{
public:
    bool IsConnected() const override { return true; }
    bool Button(int) const override { return false; }
    int AxisPerMille(int index) const override { return index == 0 ? axisValue : 0; }
    int axisValue = 0;
};
} // namespace

int main()
{
    using namespace ce::input;

    auto backendOwned = std::make_unique<FakeControllerBackend>();
    auto* backend = backendOwned.get();
    InputActionSystem system(std::move(backendOwned));

    InputAction jump;
    jump.name = "Jump";
    jump.kind = ActionKind::Digital;
    // 'Z' -- an automated test run has no reason to be holding this key.
    jump.bindings.add(InputBinding{ InputSourceKind::KeyboardKey, 'Z', 1000 });

    InputAction throttle;
    throttle.name = "Throttle";
    throttle.kind = ActionKind::Analog;
    throttle.bindings.add(InputBinding{ InputSourceKind::ControllerAxis, 0, 1000 });

    system.Bindings().actions.add(jump);
    system.Bindings().actions.add(throttle);

    system.PollOncePerFrame();
    if (system.IsActionActive("Jump")) {
        std::cerr << "Jump should not be active -- its bound key isn't held.\n";
        return 1;
    }
    if (system.GetActionValuePerMille("Throttle") != 0 || system.IsActionActive("Throttle")) {
        std::cerr << "Throttle should read 0/inactive before the fake axis moves.\n";
        return 1;
    }

    backend->axisValue = 750;
    system.PollOncePerFrame();
    if (!system.IsActionActive("Throttle") || system.GetActionValuePerMille("Throttle") != 750) {
        std::cerr << "Throttle did not report the fake controller axis's per-mille value.\n";
        return 1;
    }
    if (!system.WasActionPressed("Throttle") || system.WasActionReleased("Throttle")) {
        std::cerr << "Throttle should have edge-triggered Pressed (not Released) this tick.\n";
        return 1;
    }

    backend->axisValue = 0;
    system.PollOncePerFrame();
    if (system.IsActionActive("Throttle") || !system.WasActionReleased("Throttle") ||
        system.WasActionPressed("Throttle")) {
        std::cerr << "Throttle should have edge-triggered Released (not Pressed) and gone inactive.\n";
        return 1;
    }

    // Repeat, holding steady at the same value for a further tick --
    // edge detection must not keep firing every tick a value is unchanged.
    system.PollOncePerFrame();
    if (system.WasActionPressed("Throttle") || system.WasActionReleased("Throttle")) {
        std::cerr << "Throttle should not re-trigger an edge while its state is unchanged.\n";
        return 1;
    }

    // -- Domain::Input node registration (Input Binding System plan) --
    ce::node_system::NodeTypeRegistry registry;
    ce::node_system::RegisterCoreInputNodes(registry);
    const char* expectedTypes[] = {
        "core.input.isActionActive",
        "core.input.wasActionPressed",
        "core.input.wasActionReleased",
        "core.input.getActionValue",
    };
    for (const auto* typeName : expectedTypes) {
        const auto* descriptor = registry.Find(typeName);
        if (descriptor == nullptr || !descriptor->isHostExtern || descriptor->domain != ce::node_system::Domain::Input ||
            descriptor->frustEntryPoint.empty()) {
            std::cerr << "Node type \"" << typeName << "\" was not registered as a Domain::Input host-extern node.\n";
            return 1;
        }
    }

    std::cout << "Input action system + Domain::Input nodes passed.\n";
    return 0;
}
