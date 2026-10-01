#include "Components/Definitions/PropertySchema.h"

#include "Components/Clock.h"
#include "Components/Definitions/ComponentDefinition.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <stdexcept>
#include <utility>

namespace
{
bool numeric(PropertyType type)
{
    return type == PropertyType::Integer || type == PropertyType::Number;
}

void validateValue(const PropertyDescriptor& descriptor, const PropertyValue& value)
{
    if (value.index() != static_cast<std::size_t>(descriptor.type))
        throw std::invalid_argument("Wrong property type: " + descriptor.id);
    if (numeric(descriptor.type))
    {
        const double number = descriptor.type == PropertyType::Integer
                                  ? static_cast<double>(std::get<int>(value))
                                  : std::get<float>(value);
        if (!std::isfinite(number) ||
            (descriptor.minimum && (descriptor.minimumExclusive ? number <= *descriptor.minimum
                                                                : number < *descriptor.minimum)) ||
            (descriptor.maximum && number > *descriptor.maximum))
            throw std::invalid_argument("Property is outside its bounds: " + descriptor.id);
    }
    if (descriptor.type == PropertyType::Text &&
        std::get<std::string>(value).size() > descriptor.maxTextBytes)
        throw std::invalid_argument("Property text is too long: " + descriptor.id);
    if (!descriptor.choices.empty() && std::none_of(
                                           descriptor.choices.begin(),
                                           descriptor.choices.end(),
                                           [&](const auto& choice) { return choice.value == value; }
                                       ))
        throw std::invalid_argument("Unsupported property choice: " + descriptor.id);
}
} // namespace

std::vector<PropertyDescriptor> propertyDescriptors(const ComponentDefinition& definition)
{
    using namespace ComponentPropertyIds;
    std::vector<PropertyDescriptor> result;
    auto add = [&](
                   const char* id, const char* label, PropertyValue value, bool editable = true
               ) -> PropertyDescriptor&
    {
        result.push_back({id, label, static_cast<PropertyType>(value.index()), std::move(value)});
        result.back().editable = editable;
        return result.back();
    };
    for (const auto& item :
         {std::pair{Width, definition.layout.width}, std::pair{Height, definition.layout.height}})
    {
        auto& descriptor =
            add(item.first,
                item.first == Width ? "Width" : "Height",
                item.second,
                definition.presentation.allowResize);
        descriptor.minimum = 0.0f;
        descriptor.minimumExclusive = true;
        descriptor.maximum = std::numeric_limits<float>::max();
    }
    add(BodyLabel, "Body label", definition.presentation.bodyLabel);
    add(PinLabels, "Show pin labels", definition.presentation.showPinLabels);
    if (std::holds_alternative<GateType>(definition.behavior))
    {
        const int count = static_cast<int>(std::count_if(
            definition.layout.pins.begin(),
            definition.layout.pins.end(),
            [](const auto& pin) { return pin.direction == PinType::INPUT; }
        ));
        const bool expandable = definition.pinLayoutRule == PinLayoutRule::SymmetricGateInputs;
        auto& descriptor = add(InputCount, "Input count", count, expandable);
        descriptor.minimum = expandable ? 2.0f : static_cast<float>(count);
        descriptor.maximum = expandable ? static_cast<float>(ComponentDefinitionLimits::MaxPins - 1)
                                        : static_cast<float>(count);
    }
    if (std::holds_alternative<ManualInputBehavior>(definition.behavior))
        add(InputState, "Initial input state", definition.defaultInputState);
    if (std::holds_alternative<ClockBehavior>(definition.behavior))
    {
        auto& frequency = add(ClockFrequency, "Frequency (Hz)", definition.defaultClockFrequency);
        frequency.minimum = Clock::MINIMUM_FREQUENCY_HZ;
        frequency.maximum = std::numeric_limits<float>::max();
        add(ClockPaused, "Initially paused", definition.defaultClockPaused);
    }
    std::set<std::string> rules;
    for (const auto& rule : definition.propertyRules)
    {
        auto found = std::find_if(
            result.begin(),
            result.end(),
            [&](const auto& descriptor) { return descriptor.id == rule.id; }
        );
        if (found == result.end() || !rules.insert(rule.id).second)
            throw std::invalid_argument("Unsupported or duplicate property rule: " + rule.id);
        auto& descriptor = *found;
        if (!numeric(descriptor.type) && (rule.minimum || rule.maximum || rule.minimumExclusive))
            throw std::invalid_argument("Numeric bounds require a numeric property: " + rule.id);
        for (const auto bound : {rule.minimum, rule.maximum})
            if (bound && (!std::isfinite(*bound) || (descriptor.type == PropertyType::Integer &&
                                                     std::floor(*bound) != *bound)))
                throw std::invalid_argument("Invalid property bounds: " + rule.id);
        if (rule.minimum)
        {
            if (descriptor.minimum && (*rule.minimum < *descriptor.minimum ||
                                       (*rule.minimum == *descriptor.minimum &&
                                        descriptor.minimumExclusive && !rule.minimumExclusive)))
                throw std::invalid_argument(
                    "Property rules cannot weaken native bounds: " + rule.id
                );
            descriptor.minimum = rule.minimum;
            descriptor.minimumExclusive = rule.minimumExclusive;
        }
        else if (rule.minimumExclusive)
            throw std::invalid_argument("Exclusive minimum requires a bound: " + rule.id);
        if (rule.maximum)
        {
            if (descriptor.maximum && *rule.maximum > *descriptor.maximum)
                throw std::invalid_argument(
                    "Property rules cannot weaken native bounds: " + rule.id
                );
            descriptor.maximum = rule.maximum;
        }
        if (descriptor.minimum && descriptor.maximum &&
            (*descriptor.minimum > *descriptor.maximum ||
             (*descriptor.minimum == *descriptor.maximum && descriptor.minimumExclusive)))
            throw std::invalid_argument("Inconsistent property bounds: " + rule.id);
        descriptor.editable = descriptor.editable && rule.editable;
        descriptor.choices = rule.choices;
        std::vector<PropertyValue> choices;
        auto unconstrained = descriptor;
        unconstrained.choices.clear();
        for (const auto& choice : descriptor.choices)
        {
            if (choice.label.empty() ||
                std::find(choices.begin(), choices.end(), choice.value) != choices.end())
                throw std::invalid_argument(
                    "Choices require distinct values and labels: " + rule.id
                );
            validateValue(unconstrained, choice.value);
            choices.push_back(choice.value);
        }
    }
    for (const auto& descriptor : result)
        validateValue(descriptor, descriptor.defaultValue);
    return result;
}

void validatePropertyValues(const ComponentDefinition& definition, const PropertyValues& values)
{
    const auto descriptors = propertyDescriptors(definition);
    for (const auto& [id, value] : values)
    {
        const auto found = std::find_if(
            descriptors.begin(),
            descriptors.end(),
            [&](const auto& descriptor) { return descriptor.id == id; }
        );
        if (found == descriptors.end())
            throw std::invalid_argument("Unknown property: " + id);
        validateValue(*found, value);
    }
}

void validatePropertyOverrides(const ComponentDefinition& definition, const PropertyValues& values)
{
    validatePropertyValues(definition, values);
    for (const auto& descriptor : propertyDescriptors(definition))
        if (!descriptor.editable && values.contains(descriptor.id))
            throw std::invalid_argument("Read-only property: " + descriptor.id);
}

PropertyValues defaultPropertyValues(const ComponentDefinition& definition)
{
    PropertyValues result;
    for (const auto& descriptor : propertyDescriptors(definition))
        result.emplace(descriptor.id, descriptor.defaultValue);
    return result;
}
