#pragma once

#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <variant>
#include <vector>

struct ComponentDefinition;
using PropertyValue = std::variant<bool, int, float, std::string>;
using PropertyValues = std::map<std::string, PropertyValue, std::less<>>;
enum class PropertyType
{
    Boolean,
    Integer,
    Number,
    Text
};

namespace ComponentPropertyIds
{
inline constexpr const char* InputCount = "gate.inputCount";
inline constexpr const char* InputState = "input.initialState";
inline constexpr const char* ClockFrequency = "clock.frequencyHz";
inline constexpr const char* ClockPaused = "clock.initialPaused";
inline constexpr const char* Width = "body.width";
inline constexpr const char* Height = "body.height";
inline constexpr const char* BodyLabel = "presentation.bodyLabel";
inline constexpr const char* PinLabels = "presentation.showPinLabels";
} // namespace ComponentPropertyIds

struct PropertyChoice
{
    PropertyValue value;
    std::string label;
};

/** Definitions may narrow supported properties; native types/defaults remain authoritative. */
struct PropertyRule
{
    std::string id;
    std::optional<float> minimum, maximum;
    bool minimumExclusive = false;
    std::vector<PropertyChoice> choices;
    bool editable = true;
};

/** Complete inspector metadata; defaults are derived from the immutable declaration. */
struct PropertyDescriptor
{
    std::string id, label;
    PropertyType type;
    PropertyValue defaultValue;
    std::optional<float> minimum, maximum;
    bool minimumExclusive = false;
    std::vector<PropertyChoice> choices;
    bool editable = true;
    std::size_t maxTextBytes = 1024;
};

/** @brief Builds supported descriptors and validates declaration rules/defaults. */
std::vector<PropertyDescriptor> propertyDescriptors(const ComponentDefinition& definition);
/** @brief Validates effective values, including derived dimensions and fixed read-only defaults. */
void validatePropertyValues(const ComponentDefinition& definition, const PropertyValues& values);
/** @brief Additionally rejects explicit overrides of read-only fields. */
void validatePropertyOverrides(const ComponentDefinition& definition, const PropertyValues& values);
PropertyValues defaultPropertyValues(const ComponentDefinition& definition);
