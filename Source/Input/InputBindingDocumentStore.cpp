#include "Input/InputBindingDocumentStore.h"

#include <creation/assets/ProjectAssetService.h>

#include "Assets/AssetPackStore.h"

namespace ce::input
{
namespace
{
struct MappingContext { juce::String id; InputBindingSet bindings; };
struct MappingDocument { juce::String id, title; juce::Array<MappingContext> contexts; };

juce::String SourceToken(InputSourceKind source)
{
    switch (source) {
        case InputSourceKind::KeyboardKey: return "keyboard";
        case InputSourceKind::MouseButton: return "mouse";
        case InputSourceKind::ControllerButton: return "controller-button";
        case InputSourceKind::ControllerAxis: return "controller-axis";
        case InputSourceKind::ModifierKey: return "modifier";
    }
    return {};
}

bool ReadSource(const juce::String& token, InputSourceKind& result)
{
    if (token == "keyboard") result = InputSourceKind::KeyboardKey;
    else if (token == "mouse") result = InputSourceKind::MouseButton;
    else if (token == "controller-button") result = InputSourceKind::ControllerButton;
    else if (token == "controller-axis") result = InputSourceKind::ControllerAxis;
    else if (token == "modifier") result = InputSourceKind::ModifierKey;
    else return false;
    return true;
}

juce::String ModifierToken(int code) { return code == 0 ? "shift" : code == 1 ? "control" : "alt"; }
bool ReadModifier(const juce::String& token, int& result)
{
    if (token == "shift") result = 0;
    else if (token == "control") result = 1;
    else if (token == "alt") result = 2;
    else return false;
    return true;
}

bool ParseBindings(const juce::var& value, InputBindingSet& result, juce::String& error)
{
    result = {};
    const auto* context = value.getDynamicObject();
    const auto* actions = context != nullptr ? context->getProperty("actions").getArray() : nullptr;
    if (actions == nullptr) { error = "Input mapping context requires an actions array."; return false; }
    for (const auto& actionValue : *actions) {
        const auto* actionObject = actionValue.getDynamicObject();
        const auto* bindings = actionObject != nullptr ? actionObject->getProperty("bindings").getArray() : nullptr;
        if (actionObject == nullptr || actionObject->getProperty("name").toString().isEmpty() || bindings == nullptr) {
            error = "Input mapping action requires name and bindings."; return false;
        }
        InputAction action;
        action.name = actionObject->getProperty("name").toString();
        action.kind = actionObject->getProperty("kind").toString() == "analog" ? ActionKind::Analog : ActionKind::Digital;
        for (const auto& bindingValue : *bindings) {
            const auto* bindingObject = bindingValue.getDynamicObject();
            InputBinding binding;
            if (bindingObject == nullptr || !ReadSource(bindingObject->getProperty("source").toString(), binding.sourceKind)) {
                error = "Input mapping binding has an unsupported source."; return false;
            }
            if (binding.sourceKind == InputSourceKind::ModifierKey) {
                if (!ReadModifier(bindingObject->getProperty("code").toString(), binding.code)) {
                    error = "Input mapping modifier must be shift, control, or alt."; return false;
                }
            } else binding.code = static_cast<int>(bindingObject->getProperty("code"));
            binding.analogMagnitude = bindingObject->hasProperty("analogMagnitude")
                ? static_cast<int>(bindingObject->getProperty("analogMagnitude")) : 1000;
            action.bindings.add(binding);
        }
        result.actions.add(action);
    }
    const auto* combos = context->getProperty("combos").getArray();
    if (combos == nullptr) return true;
    for (const auto& comboValue : *combos) {
        const auto* comboObject = comboValue.getDynamicObject();
        const auto* keys = comboObject != nullptr ? comboObject->getProperty("keys").getArray() : nullptr;
        if (comboObject == nullptr || keys == nullptr || comboObject->getProperty("name").toString().isEmpty()) {
            error = "Input mapping combo requires name and keys."; return false;
        }
        InputCombo combo;
        combo.name = comboObject->getProperty("name").toString();
        for (const auto& keyValue : *keys) {
            const auto* key = keyValue.getDynamicObject();
            if (key == nullptr) { error = "Input mapping combo key must be a JSON object."; return false; }
            combo.keys.add({ static_cast<int>(key->getProperty("code")),
                             key->hasProperty("offsetMillis") ? static_cast<int>(key->getProperty("offsetMillis")) : 0 });
        }
        result.combos.add(combo);
    }
    return true;
}

bool ParseDocument(const juce::MemoryBlock& data, MappingDocument& result, juce::String& error)
{
    const auto rootValue = juce::JSON::parse(juce::String::createStringFromData(data.getData(), static_cast<int>(data.getSize())));
    const auto* root = rootValue.getDynamicObject();
    const auto* contexts = root != nullptr ? root->getProperty("contexts").getArray() : nullptr;
    if (root == nullptr || static_cast<int>(root->getProperty("formatVersion")) != 1 ||
        root->getProperty("kind").toString() != "djehuti.input-mapping" || contexts == nullptr) {
        error = "Input Mapping asset is not valid Djehuti Input Mapping JSON."; return false;
    }
    result.id = root->getProperty("id").toString();
    result.title = root->getProperty("title").toString();
    if (result.id.isEmpty()) { error = "Input Mapping asset requires id."; return false; }
    result.contexts.clear();
    for (const auto& contextValue : *contexts) {
        const auto* contextObject = contextValue.getDynamicObject();
        MappingContext context;
        context.id = contextObject != nullptr ? contextObject->getProperty("id").toString() : juce::String{};
        if (context.id.isEmpty() || !ParseBindings(contextValue, context.bindings, error)) return false;
        result.contexts.add(std::move(context));
    }
    return true;
}

juce::var WriteContext(const MappingContext& context)
{
    auto* contextObject = new juce::DynamicObject();
    contextObject->setProperty("id", context.id);
    juce::Array<juce::var> actions;
    for (const auto& action : context.bindings.actions) {
        auto* actionObject = new juce::DynamicObject();
        actionObject->setProperty("name", action.name);
        actionObject->setProperty("kind", action.kind == ActionKind::Analog ? "analog" : "digital");
        juce::Array<juce::var> bindings;
        for (const auto& binding : action.bindings) {
            auto* bindingObject = new juce::DynamicObject();
            bindingObject->setProperty("source", SourceToken(binding.sourceKind));
            bindingObject->setProperty("code", binding.sourceKind == InputSourceKind::ModifierKey ? juce::var(ModifierToken(binding.code)) : juce::var(binding.code));
            if (binding.analogMagnitude != 1000) bindingObject->setProperty("analogMagnitude", binding.analogMagnitude);
            bindings.add(juce::var(bindingObject));
        }
        actionObject->setProperty("bindings", bindings);
        actions.add(juce::var(actionObject));
    }
    contextObject->setProperty("actions", actions);
    juce::Array<juce::var> combos;
    for (const auto& combo : context.bindings.combos) {
        auto* comboObject = new juce::DynamicObject();
        comboObject->setProperty("name", combo.name);
        juce::Array<juce::var> keys;
        for (const auto& key : combo.keys) {
            auto* keyObject = new juce::DynamicObject();
            keyObject->setProperty("code", key.keyCode);
            keyObject->setProperty("offsetMillis", key.offsetMillis);
            keys.add(juce::var(keyObject));
        }
        comboObject->setProperty("keys", keys);
        combos.add(juce::var(comboObject));
    }
    contextObject->setProperty("combos", combos);
    return juce::var(contextObject);
}

bool ReadReferencedDocument(const creation::assets::ProjectSession& session, const project::GameDocumentInfo& game,
                            MappingDocument& result, juce::String& error)
{
    juce::MemoryBlock data;
    if (game.inputMappingAssetId.isNotEmpty()) {
        creation::assets::AssetRef reference { game.inputMappingAssetId, game.inputMappingAssetVersionId,
                                                creation::assets::AssetReferenceMode::exact };
        const auto* descriptor = creation::assets::ProjectAssetService::resolveAsset(session, reference);
        if (descriptor == nullptr || !session.readEntry(descriptor->logicalPath, data)) {
            error = "The Game's project Input Mapping asset is missing."; return false;
        }
    } else if (game.inputMappingPackId.isNotEmpty() && game.inputMappingPackVersion.isNotEmpty() && game.inputMappingEntryPath.isNotEmpty()) {
        if (!ce::assets::AssetPackStore::readEntry(game.inputMappingPackId, game.inputMappingPackVersion,
                                                    game.inputMappingEntryPath, data, error)) return false;
    } else { error = "The Game has no Input Mapping asset reference."; return false; }
    return ParseDocument(data, result, error);
}
}

bool InputBindingDocumentStore::load(const creation::assets::ProjectSession& session, const project::GameDocumentInfo& game,
                                     const juce::String& contextId, InputBindingSet& result, juce::String& errorMessage)
{
    MappingDocument document;
    if (!ReadReferencedDocument(session, game, document, errorMessage)) return false;
    for (const auto& context : document.contexts)
        if (context.id == contextId) { result = context.bindings; return true; }
    errorMessage = "Input Mapping \"" + document.title + "\" has no \"" + contextId + "\" context.";
    return false;
}

bool InputBindingDocumentStore::save(creation::assets::ProjectSession& session, project::GameDocumentInfo& game,
                                     const juce::String& contextId, const InputBindingSet& bindings, juce::String& errorMessage)
{
    MappingDocument document;
    if (!ReadReferencedDocument(session, game, document, errorMessage)) return false;
    bool replaced = false;
    for (auto& context : document.contexts)
        if (context.id == contextId) { context.bindings = bindings; replaced = true; break; }
    if (!replaced) { errorMessage = "Input Mapping has no editable \"" + contextId + "\" context."; return false; }

    auto* root = new juce::DynamicObject();
    root->setProperty("formatVersion", 1);
    root->setProperty("kind", "djehuti.input-mapping");
    root->setProperty("id", "game-" + game.id + "-input-mapping");
    root->setProperty("title", game.name + " Input Mapping");
    juce::Array<juce::var> contexts;
    for (const auto& context : document.contexts) contexts.add(WriteContext(context));
    root->setProperty("contexts", contexts);
    root->setProperty("playerSlotOverrides", juce::var(new juce::DynamicObject()));
    const auto json = juce::JSON::toString(juce::var(root));
    const juce::MemoryBlock data(json.toRawUTF8(), json.getNumBytesAsUTF8());
    creation::assets::ProjectAssetService::ImportOptions options;
    options.kind = creation::assets::AssetKind::metadata;
    options.displayName = game.name + " Input Mapping";
    options.category = "input-mapping";
    options.logicalPath = "engine/games/" + game.id + "/input-mappings/active.json";
    options.mediaType = "application/vnd.djehuti.input-mapping+json";
    options.sourceApp = "Djehuti Engine";
    options.sourceTool = "Input Bindings";
    options.description = "Djehuti Input Mapping";
    creation::assets::AssetDescriptor saved;
    if (!creation::assets::ProjectAssetService::saveGeneratedAsset(session, data, options, saved, errorMessage)) return false;
    game.inputMappingAssetId = saved.id;
    game.inputMappingAssetVersionId = saved.versionId;
    game.inputMappingPackId = {};
    game.inputMappingPackVersion = {};
    game.inputMappingEntryPath = {};
    return true;
}
} // namespace ce::input
