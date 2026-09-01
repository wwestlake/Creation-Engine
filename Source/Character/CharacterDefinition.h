#pragma once

#include <map>
#include <string>
#include <vector>

#include <juce_data_structures/juce_data_structures.h>

namespace ce::character
{

enum class Role { Player, AI, NonPlayer, Shared };

struct CharacterDefinition
{
    static constexpr int currentSchemaVersion = 1;

    int schemaVersion = currentSchemaVersion;
    std::string id = "character";
    std::string name = "New Character";
    std::string description;
    std::string baseType = "Humanoid";
    Role role = Role::Player;

    // Normalized authoring values. The importer maps these to the source
    // rig's measured ranges, so the asset remains independent of FBX units.
    std::map<std::string, float, std::less<>> bodyParameters{
        { "age", 0.5f }, { "height", 0.5f }, { "muscle", 0.5f }, { "weight", 0.5f } };
    std::map<std::string, std::string, std::less<>> appearance{
        { "skin", "default" }, { "hair", "default" }, { "hairColor", "brown" } };
    std::vector<std::string> equipment;
    std::vector<std::string> capabilities;
    std::map<std::string, float, std::less<>> skills;

    juce::ValueTree toValueTree() const;
    static bool fromValueTree(const juce::ValueTree& state, CharacterDefinition& result, juce::String& error);
    juce::String serialize() const;
    static bool deserialize(const juce::String& text, CharacterDefinition& result, juce::String& error);
};

} // namespace ce::character
