#include "Input/InputBindingDocumentStore.h"

#include <juce_data_structures/juce_data_structures.h>

namespace ce::input
{
namespace
{
// Same small ValueTree<->XML<->VFS-entry idiom EngineGameDocument.cpp
// already uses for games.xml/scene documents -- duplicated locally rather
// than factored into a shared header, since it's two short functions and
// no shared utility for this idiom exists yet anywhere in this codebase.
juce::MemoryBlock ToMemory(const juce::ValueTree& tree)
{
    const auto xml = tree.createXml();
    const auto text = xml != nullptr ? xml->toString() : juce::String{};
    return { text.toRawUTF8(), static_cast<std::size_t>(text.getNumBytesAsUTF8()) };
}

bool ReadTree(creation::assets::ProjectSession& session, const juce::String& path,
              juce::ValueTree& result, juce::String& error)
{
    juce::MemoryBlock data;
    if (!session.readEntry(path, data)) {
        error = "Missing Engine document: " + path;
        return false;
    }
    const auto text = juce::String::createStringFromData(data.getData(), static_cast<int>(data.getSize()));
    const auto xml = juce::XmlDocument::parse(text);
    if (xml == nullptr) {
        error = "Invalid Engine document: " + path;
        return false;
    }
    result = juce::ValueTree::fromXml(*xml);
    return true;
}

juce::ValueTree WriteBinding(const InputBinding& binding)
{
    juce::ValueTree node("Binding");
    node.setProperty("sourceKind", static_cast<int>(binding.sourceKind), nullptr);
    node.setProperty("code", binding.code, nullptr);
    node.setProperty("analogMagnitude", binding.analogMagnitude, nullptr);
    return node;
}

InputBinding ReadBinding(const juce::ValueTree& node)
{
    InputBinding binding;
    binding.sourceKind = static_cast<InputSourceKind>(static_cast<int>(node.getProperty("sourceKind", 0)));
    binding.code = node.getProperty("code", 0);
    binding.analogMagnitude = node.getProperty("analogMagnitude", 1000);
    return binding;
}

juce::ValueTree WriteAction(const InputAction& action)
{
    juce::ValueTree node("Action");
    node.setProperty("name", action.name, nullptr);
    node.setProperty("kind", static_cast<int>(action.kind), nullptr);
    for (const auto& binding : action.bindings) node.addChild(WriteBinding(binding), -1, nullptr);
    return node;
}

InputAction ReadAction(const juce::ValueTree& node)
{
    InputAction action;
    action.name = node.getProperty("name").toString();
    action.kind = static_cast<ActionKind>(static_cast<int>(node.getProperty("kind", 0)));
    for (const auto child : node)
        if (child.hasType("Binding")) action.bindings.add(ReadBinding(child));
    return action;
}

juce::ValueTree WriteComboKeyPress(const ComboKeyPress& key)
{
    juce::ValueTree node("Key");
    node.setProperty("code", key.keyCode, nullptr);
    node.setProperty("offsetMillis", key.offsetMillis, nullptr);
    return node;
}

ComboKeyPress ReadComboKeyPress(const juce::ValueTree& node)
{
    ComboKeyPress key;
    key.keyCode = node.getProperty("code", 0);
    key.offsetMillis = node.getProperty("offsetMillis", 0);
    return key;
}

juce::ValueTree WriteCombo(const InputCombo& combo)
{
    juce::ValueTree node("Combo");
    node.setProperty("name", combo.name, nullptr);
    for (const auto& key : combo.keys) node.addChild(WriteComboKeyPress(key), -1, nullptr);
    return node;
}

InputCombo ReadCombo(const juce::ValueTree& node)
{
    InputCombo combo;
    combo.name = node.getProperty("name").toString();
    for (const auto child : node)
        if (child.hasType("Key")) combo.keys.add(ReadComboKeyPress(child));
    return combo;
}
} // namespace

juce::String InputBindingDocumentStore::path(const project::GameDocumentInfo& game)
{
    return "engine/games/" + game.id + "/input-bindings.xml";
}

bool InputBindingDocumentStore::load(creation::assets::ProjectSession& session,
                                      const project::GameDocumentInfo& game,
                                      InputBindingSet& result,
                                      juce::String& errorMessage)
{
    result.actions.clear();
    result.combos.clear();
    const auto documentPath = path(game);
    if (!session.containsEntry(documentPath)) return true; // no bindings authored yet -- not a failure.

    juce::ValueTree document;
    if (!ReadTree(session, documentPath, document, errorMessage)) return false;
    if (!document.hasType("CreationEngineInputBindings")) {
        errorMessage = "Input bindings document has the wrong document type.";
        return false;
    }
    for (const auto child : document) {
        if (child.hasType("Action")) result.actions.add(ReadAction(child));
        else if (child.hasType("Combo")) result.combos.add(ReadCombo(child));
    }
    return true;
}

bool InputBindingDocumentStore::save(creation::assets::ProjectSession& session,
                                      const project::GameDocumentInfo& game,
                                      const InputBindingSet& bindings,
                                      juce::String& errorMessage)
{
    juce::ValueTree document("CreationEngineInputBindings");
    document.setProperty("formatVersion", 1, nullptr);
    for (const auto& action : bindings.actions) document.addChild(WriteAction(action), -1, nullptr);
    for (const auto& combo : bindings.combos) document.addChild(WriteCombo(combo), -1, nullptr);
    if (!session.writeEntry(path(game), ToMemory(document))) {
        errorMessage = "Could not save input bindings for game \"" + game.name + "\".";
        return false;
    }
    return true;
}
} // namespace ce::input
