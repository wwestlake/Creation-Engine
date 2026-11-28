#include "GameplayProtocol.h"

#include <juce_cryptography/juce_cryptography.h>
#include <array>
#include <cmath>
#include <cstring>

namespace ce::gameplay
{
Message Object(std::initializer_list<std::pair<const char*, Message>> fields)
{
    auto object = juce::DynamicObject::Ptr(new juce::DynamicObject);
    for (const auto& [key, value] : fields) object->setProperty(key, value);
    return object.get();
}

Message Get(const Message& message, const char* property)
{
    return message.getProperty(property, {});
}

void Put(Message& message, const char* property, Message value)
{
    message.getDynamicObject()->setProperty(property, std::move(value));
}

std::optional<Message> Decode(std::string_view wire)
{
    if (wire.empty() || wire.size() > maximumPacketBytes || wire.find('\0') != std::string_view::npos)
        return {};
    if (!juce::CharPointer_UTF8::isValidString(wire.data(), static_cast<int>(wire.size())))
        return {};

    // Bound JSON nesting before handing network input to the recursive parser.
    int depth = 0;
    bool quoted = false;
    bool escaped = false;
    bool complete = false;
    for (const char ch : wire) {
        if (complete) {
            if (ch != ' ' && ch != '\n' && ch != '\r' && ch != '\t') return {};
            continue;
        }
        if (escaped) { escaped = false; continue; }
        if (quoted && ch == '\\') { escaped = true; continue; }
        if (ch == '"') { quoted = !quoted; continue; }
        if (quoted) continue;
        if ((ch == '[' || ch == '{') && ++depth > 12) return {};
        if (ch == ']' || ch == '}') {
            if (--depth < 0) return {};
            complete = depth == 0;
        }
    }
    if (!complete || depth != 0 || quoted) return {};
    Message message;
    if (!juce::JSON::parse(juce::String::fromUTF8(wire.data(), static_cast<int>(wire.size())), message).wasOk()
        || message.getDynamicObject() == nullptr)
        return {};
    return message;
}

std::string Encode(const Message& message)
{
    return juce::JSON::toString(message, true).toStdString();
}

bool ExactFields(const Message& message, std::initializer_list<const char*> fields)
{
    const auto* object = message.getDynamicObject();
    if (object == nullptr || object->getProperties().size() != static_cast<int>(fields.size())) return false;
    for (const auto* key : fields)
        if (!object->hasProperty(key)) return false;
    return true;
}

std::optional<std::int64_t> Integer(const Message& value, std::int64_t low, std::int64_t high)
{
    if (!value.isInt() && !value.isInt64()) return {};
    const auto result = static_cast<std::int64_t>(static_cast<juce::int64>(value));
    if (result < low || result > high) return {};
    return result;
}

std::optional<juce::String> Text(const Message& value, int maximum, bool allowEmpty)
{
    if (!value.isString()) return {};
    const auto text = value.toString();
    if ((!allowEmpty && text.isEmpty()) || text.getNumBytesAsUTF8() > static_cast<std::size_t>(maximum)) return {};
    return text;
}

bool Identifier(const juce::String& value, int maximum)
{
    if (value.isEmpty() || value.length() > maximum) return false;
    for (auto position = value.getCharPointer(); !position.isEmpty();) {
        const auto c = position.getAndAdvance();
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')
              || c == '.' || c == '-' || c == '_'))
            return false;
    }
    return true;
}

std::optional<double> Number(const Message& value, double limit)
{
    if (!value.isDouble() && !value.isInt() && !value.isInt64()) return {};
    const auto result = static_cast<double>(value);
    if (!std::isfinite(result) || std::abs(result) > limit) return {};
    return result;
}

juce::String MakeProof(const juce::String& secret, const juce::String& challenge,
                        const juce::String& world, const juce::String& player)
{
    const auto text = "djehuti-gameplay-auth-v1\n" + challenge + "\n" + world + "\n" + player;
    const auto key = secret.toStdString();
    std::array<std::uint8_t, 64> block{};
    if (key.size() > block.size()) {
        const auto digest = juce::SHA256(key.data(), key.size()).getRawData();
        std::memcpy(block.data(), digest.getData(), digest.getSize());
    } else {
        std::memcpy(block.data(), key.data(), key.size());
    }

    std::array<std::uint8_t, 64> inner{}, outer{};
    for (std::size_t i = 0; i < block.size(); ++i) {
        inner[i] = block[i] ^ 0x36;
        outer[i] = block[i] ^ 0x5c;
    }
    juce::MemoryBlock first(inner.data(), inner.size());
    first.append(text.toRawUTF8(), text.getNumBytesAsUTF8());
    const auto digest = juce::SHA256(first).getRawData();
    juce::MemoryBlock second(outer.data(), outer.size());
    second.append(digest.getData(), digest.getSize());
    return juce::SHA256(second).toHexString();
}

bool VerifyProof(const juce::String& expected, const juce::String& received)
{
    const auto left = expected.toStdString();
    const auto right = received.toStdString();
    if (left.size() != 64 || right.size() != left.size()) return false;
    int difference = 0;
    for (std::size_t i = 0; i < left.size(); ++i)
        difference |= static_cast<unsigned char>(left[i]) ^ static_cast<unsigned char>(right[i]);
    return difference == 0;
}
} // namespace ce::gameplay
