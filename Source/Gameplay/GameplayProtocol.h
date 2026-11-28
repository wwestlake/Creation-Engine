#pragma once

#include <juce_core/juce_core.h>
#include <cstdint>
#include <optional>
#include <string_view>

namespace ce::gameplay
{
constexpr int protocolVersion = 1;
constexpr std::size_t maximumPacketBytes = 64 * 1024;
constexpr std::size_t maximumPendingBytes = 512 * 1024;
constexpr int maximumConnections = 32;
constexpr int maximumPlayers = 256;
constexpr int maximumEntities = 512;
constexpr int maximumRequestsPerTick = 4;
constexpr int maximumEventsPerPump = 96;

using Message = juce::var;

Message Object(std::initializer_list<std::pair<const char*, Message>> fields = {});
Message Get(const Message& message, const char* property);
void Put(Message& message, const char* property, Message value);
std::optional<Message> Decode(std::string_view wire);
std::string Encode(const Message& message);
bool ExactFields(const Message& message, std::initializer_list<const char*> fields);
std::optional<std::int64_t> Integer(const Message& value, std::int64_t low, std::int64_t high);
std::optional<juce::String> Text(const Message& value, int maximum, bool allowEmpty = false);
bool Identifier(const juce::String& value, int maximum = 64);
std::optional<double> Number(const Message& value, double limit);
juce::String MakeProof(const juce::String& secret, const juce::String& challenge,
                        const juce::String& world, const juce::String& player);
bool VerifyProof(const juce::String& expected, const juce::String& received);
} // namespace ce::gameplay
