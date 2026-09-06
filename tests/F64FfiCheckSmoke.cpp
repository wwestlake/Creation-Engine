#include "creation/frust/PluginRuntime.h"

#include <cstdint>
#include <cstring>
#include <iostream>

// Real, empirical verification of f64 crossing the extern fn FFI boundary in
// BOTH directions, bit-exact -- not just "looks numerically right," which
// could silently mask a register-width/calling-convention mismatch the same
// way naive bool-truthiness checking could (see LANGUAGE_GAPS.md #9's own
// bool-FFI verification, which this mirrors exactly). Nothing in this
// codebase has tested f64 across this boundary before -- EngineFrustHost.h's
// own comment says so directly ("no equivalent dedicated f64-FFI
// verification exists"). This is Phase 1 of the "real main pipeline" plan's
// first, most basic gate: the new transform get/set nodes are all f64 --
// if this doesn't pass cleanly, nothing built on top of it can be trusted.

namespace {
constexpr double kExpectedArgument = 3.14159265358979;
constexpr double kExpectedReturn = 2.71828182845905;

extern "C" std::int64_t HostReceivesF64(double value) {
    return std::memcmp(&value, &kExpectedArgument, sizeof(double)) == 0 ? 1 : 0;
}

extern "C" double HostReturnsF64() {
    return kExpectedReturn;
}
} // namespace

int main() {
    using creation::frust::PluginRuntime;

    PluginRuntime runtime("creation-engine");
    runtime.registerHostFunction("host_receives_f64", reinterpret_cast<void*>(&HostReceivesF64));
    runtime.registerHostFunction("host_returns_f64", reinterpret_cast<void*>(&HostReturnsF64));

    std::string error;
    if (!runtime.load(CE_F64_FFI_CHECK_PLUGIN, error)) {
        std::cerr << "Could not load f64 FFI check plugin: " << error << '\n';
        return 1;
    }

    // Direction 1: FRust -> C++ (argument passing).
    using CheckArgumentFn = std::int64_t (*)();
    const auto checkArgument = reinterpret_cast<CheckArgumentFn>(runtime.getFunction("check_argument_direction"));
    if (!checkArgument) {
        std::cerr << "Could not resolve check_argument_direction\n";
        return 1;
    }
    if (checkArgument() != 1) {
        std::cerr << "f64 argument did NOT cross the FFI boundary with the exact expected bit pattern.\n";
        return 1;
    }

    // Direction 2: C++ -> FRust -> back to C++ (return value passing).
    using CheckReturnFn = double (*)();
    const auto checkReturn = reinterpret_cast<CheckReturnFn>(runtime.getFunction("check_return_direction"));
    if (!checkReturn) {
        std::cerr << "Could not resolve check_return_direction\n";
        return 1;
    }
    const double returned = checkReturn();
    if (std::memcmp(&returned, &kExpectedReturn, sizeof(double)) != 0) {
        std::cerr << "f64 return value did NOT cross the FFI boundary with the exact expected bit pattern "
                   << "(got " << returned << ", expected " << kExpectedReturn << ").\n";
        return 1;
    }

    std::cout << "f64-across-FFI check passed: bit-exact in both directions.\n";
    return 0;
}
