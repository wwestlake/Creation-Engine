#pragma once

#include <juce_core/juce_core.h>

#include <charconv>
#include <initializer_list>
#include <map>
#include <string>
#include <string_view>

namespace ce::developer
{
using Value = juce::var;
using String = juce::String;

inline Value Object(std::initializer_list<std::pair<const char*, Value>> values = {})
{
    auto object = juce::DynamicObject::Ptr(new juce::DynamicObject());
    for (const auto& [key, value] : values)
        object->setProperty(juce::Identifier(key), value);
    return Value(object.get());
}

inline void Put(Value& object, const char* name, Value value)
{
    object.getDynamicObject()->setProperty(juce::Identifier(name), std::move(value));
}

inline Value Get(const Value& object, const char* name)
{
    return object.getProperty(juce::Identifier(name), {});
}

inline Value Array(std::initializer_list<Value> values = {})
{
    juce::Array<Value> array;
    for (const auto& value : values)
        array.add(value);
    return Value(std::move(array));
}

inline std::string Text(const Value& value) { return value.toString().toStdString(); }

inline String Utf8(std::string_view text)
{
    if (text.empty())
        return {};
    return String::fromUTF8(text.data(), static_cast<int>(text.size()));
}

inline Value Decimal(std::uint64_t value) { return String(std::to_string(value)); }

inline Value Canonical(const Value& value)
{
    if (const auto* object = value.getDynamicObject()) {
        std::map<String, Value> sorted;
        for (const auto& item : object->getProperties()) sorted.emplace(item.name.toString(), item.value);
        auto result = Object();
        for (const auto& [name, item] : sorted)
            result.getDynamicObject()->setProperty(juce::Identifier(name), Canonical(item));
        return result;
    }
    if (const auto* array = value.getArray()) {
        juce::Array<Value> items;
        for (const auto& item : *array) items.add(Canonical(item));
        return Value(std::move(items));
    }
    return value;
}

struct Failure {
    const char* code;
    const char* message;
    int httpStatus;
    Value details;
};

[[noreturn]] inline void Reject(const char* code, const char* message, int httpStatus = 400, Value details = {})
{
    throw Failure{code, message, httpStatus, std::move(details)};
}

inline Value Error(const Failure& error)
{
    auto value = Object({{"code", error.code}, {"message", error.message}});
    if (!error.details.isVoid())
        Put(value, "details", error.details);
    return value;
}

template <typename Integer>
Integer ParseDecimal(const Value& value)
{
    if (!value.isString())
        Reject("invalid_input", "Expected a decimal string.");
    const auto text = Text(value);
    Integer number{};
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), number);
    if (error != std::errc{} || end != text.data() + text.size() || text != std::to_string(number))
        Reject("invalid_input", "Invalid or out-of-range decimal string.");
    return number;
}
} // namespace ce::developer
