#include "CharacterDefinition.h"

#include <memory>

namespace ce::character
{
namespace
{
juce::String roleName(Role role)
{
    switch (role) { case Role::Player: return "player"; case Role::AI: return "ai"; case Role::NonPlayer: return "nonPlayer"; case Role::Shared: return "shared"; }
    return "player";
}

Role parseRole(const juce::String& value)
{
    if (value == "ai") return Role::AI;
    if (value == "nonPlayer") return Role::NonPlayer;
    if (value == "shared") return Role::Shared;
    return Role::Player;
}
}

juce::ValueTree CharacterDefinition::toValueTree() const
{
    juce::ValueTree state("CharacterDefinition");
    state.setProperty("schemaVersion", schemaVersion, nullptr);
    state.setProperty("id", juce::String(id), nullptr);
    state.setProperty("name", juce::String(name), nullptr);
    state.setProperty("description", juce::String(description), nullptr);
    state.setProperty("baseType", juce::String(baseType), nullptr);
    state.setProperty("role", roleName(role), nullptr);

    juce::ValueTree body("Body");
    for (const auto& [key, value] : bodyParameters) body.setProperty(juce::Identifier(key), value, nullptr);
    state.addChild(body, -1, nullptr);

    juce::ValueTree looks("Appearance");
    for (const auto& [key, value] : appearance) looks.setProperty(juce::Identifier(key), juce::String(value), nullptr);
    state.addChild(looks, -1, nullptr);

    juce::ValueTree equipmentState("Equipment");
    for (const auto& item : equipment) { juce::ValueTree entry("Item"); entry.setProperty("id", juce::String(item), nullptr); equipmentState.addChild(entry, -1, nullptr); }
    state.addChild(equipmentState, -1, nullptr);

    juce::ValueTree capabilitiesState("Capabilities");
    for (const auto& capability : capabilities) { juce::ValueTree entry("Capability"); entry.setProperty("id", juce::String(capability), nullptr); capabilitiesState.addChild(entry, -1, nullptr); }
    state.addChild(capabilitiesState, -1, nullptr);

    juce::ValueTree skillsState("Skills");
    for (const auto& [key, value] : skills) skillsState.setProperty(juce::Identifier(key), value, nullptr);
    state.addChild(skillsState, -1, nullptr);
    return state;
}

bool CharacterDefinition::fromValueTree(const juce::ValueTree& state, CharacterDefinition& result, juce::String& error)
{
    if (!state.isValid() || state.getType() != juce::Identifier("CharacterDefinition")) { error = "Not a CharacterDefinition asset."; return false; }
    const int version = static_cast<int>(state.getProperty("schemaVersion"));
    if (version <= 0 || version > currentSchemaVersion) { error = "Unsupported CharacterDefinition schema version."; return false; }
    result = {};
    result.schemaVersion = version;
    result.id = state.getProperty("id").toString().toStdString();
    result.name = state.getProperty("name").toString().toStdString();
    result.description = state.getProperty("description").toString().toStdString();
    result.baseType = state.getProperty("baseType").toString().toStdString();
    result.role = parseRole(state.getProperty("role").toString());
    if (auto body = state.getChildWithName("Body"); body.isValid()) for (const auto& [key, unused] : result.bodyParameters) result.bodyParameters[key] = static_cast<float>(body.getProperty(juce::Identifier(key), unused));
    if (auto looks = state.getChildWithName("Appearance"); looks.isValid()) for (const auto& [key, unused] : result.appearance) result.appearance[key] = looks.getProperty(juce::Identifier(key), juce::String(unused)).toString().toStdString();
    if (auto items = state.getChildWithName("Equipment"); items.isValid()) for (int i = 0; i < items.getNumChildren(); ++i) result.equipment.push_back(items.getChild(i).getProperty("id").toString().toStdString());
    if (auto caps = state.getChildWithName("Capabilities"); caps.isValid()) for (int i = 0; i < caps.getNumChildren(); ++i) result.capabilities.push_back(caps.getChild(i).getProperty("id").toString().toStdString());
    if (auto skillState = state.getChildWithName("Skills"); skillState.isValid()) for (const auto& [key, unused] : result.skills) result.skills[key] = static_cast<float>(skillState.getProperty(juce::Identifier(key), unused));
    return true;
}

juce::String CharacterDefinition::serialize() const
{
    if (auto xml = toValueTree().createXml()) return xml->toString();
    return {};
}

bool CharacterDefinition::deserialize(const juce::String& text, CharacterDefinition& result, juce::String& error)
{
    auto xml = juce::XmlDocument::parse(text);
    if (xml == nullptr) { error = "CharacterDefinition XML is invalid."; return false; }
    return fromValueTree(juce::ValueTree::fromXml(*xml), result, error);
}

} // namespace ce::character
