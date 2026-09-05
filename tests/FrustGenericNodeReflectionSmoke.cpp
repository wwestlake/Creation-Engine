#include "creation/frust/PluginRuntime.h"

#include <iostream>
#include <string>

// Phase 1 of the "real monomorphized generics for Schematic nodes" plan:
// confirms Codegen.h's compileNodeReflection now exposes genericness at all
// (a "genericParams" list per node, a "genericParam" name per pin that
// matches one of the function's own type parameters) instead of silently
// collapsing a generic node to an indistinguishable-from-nongeneric "any"
// pin, as it did before this change. No turbofish call site is needed in
// the plugin for this: reflection runs off the parsed AST before Pass
// 1/1.5/2, regardless of whether the generic function is ever monomorphized
// -- see Codegen.h's own compileNodeReflection ordering.
int main()
{
    using creation::frust::PluginRuntime;

    PluginRuntime runtime("creation-engine");
    std::string error;
    if (!runtime.load(CE_GENERIC_NODE_REFLECTION_PLUGIN, error)) {
        std::cerr << "Could not load generic node reflection plugin: " << error << '\n';
        return 1;
    }

    const auto libraries = runtime.nodeLibraries(PluginRuntime::defaultPluginKey);
    if (libraries.size() != 1) {
        std::cerr << "Expected exactly one reflected node library, found " << libraries.size() << ".\n";
        return 1;
    }
    const auto& descriptorJson = libraries.front().descriptorJson;

    // JUCE's JSON::toString (used by MergeNodeReflection to re-serialize the
    // compiler's raw reflection JSON into the manifest) inserts a space
    // after every colon -- confirmed by actually running this test and
    // reading its real output, not assumed.
    if (descriptorJson.find("\"typeName\": \"identity\"") == std::string::npos) {
        std::cerr << "Reflected manifest is missing the identity node: " << descriptorJson << '\n';
        return 1;
    }
    if (descriptorJson.find("\"genericParams\": [\"T\"]") == std::string::npos) {
        std::cerr << "Reflected manifest did not expose identity's genericParams: " << descriptorJson << '\n';
        return 1;
    }
    // Both the input pin "x" and the output pin "value" are typed `T` in
    // the source -- both must carry "genericParam": "T" in their own pin
    // object, not just once anywhere in the document.
    const auto firstGenericParam = descriptorJson.find("\"genericParam\": \"T\"");
    if (firstGenericParam == std::string::npos) {
        std::cerr << "Reflected manifest did not tag any pin with genericParam: " << descriptorJson << '\n';
        return 1;
    }
    const auto secondGenericParam = descriptorJson.find("\"genericParam\": \"T\"", firstGenericParam + 1);
    if (secondGenericParam == std::string::npos) {
        std::cerr << "Reflected manifest tagged only one of identity's two T-typed pins: " << descriptorJson << '\n';
        return 1;
    }

    std::cout << "FRust generic node reflection passed.\n";
    return 0;
}
