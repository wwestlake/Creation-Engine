#include <creation/frust/PluginRuntime.h>

#include <iostream>
#include <string>

int main()
{
    creation::frust::PluginRuntime runtime("creation-engine");
    std::string error;

    if (!runtime.load("alpha", CE_RUNTIME_ALPHA_PLUGIN, error) ||
        !runtime.load("beta", CE_RUNTIME_BETA_PLUGIN, error))
    {
        std::cerr << "Could not load independent FRust plugins: " << error << '\n';
        return 1;
    }

    if (!runtime.isLoaded("alpha") || !runtime.isLoaded("beta") || runtime.loadedPluginKeys().size() != 2)
    {
        std::cerr << "FRust runtime did not retain both keyed plugins." << '\n';
        return 1;
    }

    if (runtime.callEvent("alpha", 3, 7) != 307 || runtime.callEvent("beta", 3, 7) != 703)
    {
        std::cerr << "FRust runtime did not dispatch to the requested plugin." << '\n';
        return 1;
    }

    if (!runtime.reload("alpha", error) || runtime.callEvent("beta", 4, 2) != 204)
    {
        std::cerr << "Reloading one FRust plugin disturbed another: " << error << '\n';
        return 1;
    }

    runtime.unload("alpha");
    if (runtime.isLoaded("alpha") || !runtime.isLoaded("beta") || runtime.callEvent("beta", 5, 1) != 105)
    {
        std::cerr << "Unloading one FRust plugin disturbed another." << '\n';
        return 1;
    }

    std::cout << "Creation Suite multi-plugin runtime isolation passed." << '\n';
    return 0;
}
