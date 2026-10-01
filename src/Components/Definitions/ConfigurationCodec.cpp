#include "Components/Definitions/ConfigurationCodec.h"

#include "Components/Definitions/ComponentCatalog.h"

#include <charconv>
#include <cmath>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace
{
constexpr std::size_t maximumBytes = 8 * 1024 * 1024;

void require(bool condition)
{
    if (!condition)
        throw std::invalid_argument("Invalid component configuration record.");
}

std::string token(std::istream& stream)
{
    std::string value;
    require(static_cast<bool>(stream >> value));
    return value;
}

void expect(std::istream& stream, const char* expected)
{
    require(token(stream) == expected);
}

std::string quoted(std::istream& stream)
{
    stream >> std::ws;
    require(stream.peek() == '"');
    std::string value;
    require(static_cast<bool>(stream >> std::quoted(value)));
    return value;
}

template <typename T>
T number(std::istream& stream)
{
    const auto value = token(stream);
    require(value.size() <= 64);
    T result;
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
    require(parsed.ec == std::errc{} && parsed.ptr == value.data() + value.size());
    if constexpr (std::is_floating_point_v<T>)
        require(std::isfinite(result));
    return result;
}

std::size_t count(std::istream& stream, std::size_t maximum)
{
    auto value = number<unsigned int>(stream);
    require(value <= maximum);
    return value;
}

void validateRecord(const ComponentCatalog& catalog, const ComponentConfigurationRecord& record)
{
    const auto* definition = catalog.find(record.definition.id);
    require(
        definition && record.definition.version != 0 &&
        definition->identity.version == record.definition.version
    );
    const auto resolved = resolveConfiguration(*definition, record.configuration);
    require(
        resolved.configuration == record.configuration
    ); // Stored pins must already carry canonical IDs/labels.
}
} // namespace

std::string
encodeConfiguration(const ComponentCatalog& catalog, const ComponentConfigurationRecord& record)
{
    validateRecord(catalog, record);
    std::ostringstream stream;
    stream.imbue(std::locale::classic());
    stream << std::setprecision(std::numeric_limits<float>::max_digits10);
    stream << "logic-component-config 1\ndefinition " << std::quoted(record.definition.id) << ' '
           << record.definition.version;
    stream << "\nproperties " << record.configuration.overrides.size() << '\n';
    for (const auto& [id, value] : record.configuration.overrides)
    {
        stream << std::quoted(id) << ' ';
        std::visit(
            [&](const auto& item)
            {
                using T = std::decay_t<decltype(item)>;
                if constexpr (std::is_same_v<T, bool>)
                    stream << "bool " << (item ? 1 : 0);
                else if constexpr (std::is_same_v<T, int>)
                    stream << "int " << item;
                else if constexpr (std::is_same_v<T, float>)
                    stream << "number " << item;
                else
                    stream << "text " << std::quoted(item);
            },
            value
        );
        stream << '\n';
    }
    stream << "pins " << (record.configuration.pinLayout ? 1 : 0) << '\n';
    if (record.configuration.pinLayout)
    {
        stream << record.configuration.pinLayout->size() << '\n';
        for (const auto& pin : *record.configuration.pinLayout)
        {
            stream << std::quoted(pin.id) << ' ' << std::quoted(pin.label) << ' '
                   << (pin.direction == PinType::INPUT ? "input" : "output") << ' ' << pin.index
                   << ' ' << pin.anchor.x << ' ' << pin.anchor.y << ' ' << pin.lead.size();
            for (const auto point : pin.lead)
                stream << ' ' << point.x << ' ' << point.y;
            stream << '\n';
        }
    }
    stream << "end\n";
    auto result = stream.str();
    require(result.size() <= maximumBytes);
    return result;
}

ComponentConfigurationRecord
decodeConfiguration(const ComponentCatalog& catalog, std::string_view text)
{
    require(text.size() <= maximumBytes);
    std::istringstream stream{std::string(text)};
    stream.imbue(std::locale::classic());
    expect(stream, "logic-component-config");
    require(number<unsigned int>(stream) == 1);
    expect(stream, "definition");
    ComponentConfigurationRecord record;
    record.definition.id = quoted(stream);
    record.definition.version = number<std::uint32_t>(stream);
    expect(stream, "properties");
    const auto properties = count(stream, 64);
    for (std::size_t i = 0; i < properties; ++i)
    {
        const auto id = quoted(stream);
        const auto type = token(stream);
        PropertyValue value;
        if (type == "bool")
        {
            const int boolean = number<int>(stream);
            require(boolean == 0 || boolean == 1);
            value = boolean != 0;
        }
        else if (type == "int")
            value = number<int>(stream);
        else if (type == "number")
            value = number<float>(stream);
        else if (type == "text")
            value = quoted(stream);
        else
            require(false);
        require(record.configuration.overrides.emplace(id, std::move(value)).second);
    }
    expect(stream, "pins");
    const auto hasPins = count(stream, 1);
    if (hasPins)
    {
        record.configuration.pinLayout.emplace();
        const auto pins = count(stream, ComponentDefinitionLimits::MaxPins);
        for (std::size_t i = 0; i < pins; ++i)
        {
            PinDefinition pin;
            pin.id = quoted(stream);
            pin.label = quoted(stream);
            const auto direction = token(stream);
            require(direction == "input" || direction == "output");
            pin.direction = direction == "input" ? PinType::INPUT : PinType::OUTPUT;
            pin.index = number<unsigned int>(stream);
            pin.anchor.x = number<int>(stream);
            pin.anchor.y = number<int>(stream);
            const auto lead = count(stream, 256);
            for (std::size_t j = 0; j < lead; ++j)
                pin.lead.push_back({number<int>(stream), number<int>(stream)});
            record.configuration.pinLayout->push_back(std::move(pin));
        }
    }
    expect(stream, "end");
    stream >> std::ws;
    require(stream.eof());
    validateRecord(catalog, record);
    return record;
}
