#pragma once

#include "GameplayProtocol.h"
#include "DeveloperApi/DeveloperApi.h"
#include <juce_data_structures/juce_data_structures.h>
#include <map>

namespace ce::gameplay
{
struct StoreSettings {
    juce::String worldId;
    juce::String provider = "memory";
    juce::String directory;
    int checkpointTicks = 30;
};

struct PlayerState {
    juce::String entityId;
    juce::String slotId;
};

struct Checkpoint {
    juce::String game;
    juce::String scene;
    std::uint64_t tick = 0;
    juce::ValueTree state;
    std::map<juce::String, PlayerState> players;
    std::map<juce::String, std::int64_t> application;
};

class GameplayStore final
{
public:
    explicit GameplayStore(StoreSettings settings);
    bool Open(juce::String& error);
    bool Restore(Checkpoint& checkpoint, juce::String& error);
    bool ReadScene(const juce::String& scene, juce::ValueTree& state, juce::String& error) const;
    bool Commit(const Checkpoint& checkpoint, juce::String& error);

    [[nodiscard]] const StoreSettings& Settings() const noexcept { return settings_; }
    [[nodiscard]] std::uint64_t Revision() const noexcept { return revision_; }
    [[nodiscard]] developer::PersistenceMount InspectableMount() const;

private:
    StoreSettings settings_;
    std::shared_ptr<persistence::StorageProvider> provider_;
    std::shared_ptr<persistence::Session> session_;
    std::uint64_t revision_ = 0;
};
} // namespace ce::gameplay
