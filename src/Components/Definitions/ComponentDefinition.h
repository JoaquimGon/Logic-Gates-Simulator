#pragma once
#include "Components/Definitions/PresentationGeometry.h"
#include "Components/Definitions/PropertySchema.h"
#include "Components/Gate.h"
#include "Components/Latch.h"
#include "Components/PinTypes.h"
#include "Geometry/GridCoords.h"

#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace ComponentDefinitionLimits
{
inline constexpr int MaxPins = 256;
}

struct ManualInputBehavior
{
};

struct ClockBehavior
{
};

using NativeBehavior = std::variant<GateType, ManualInputBehavior, ClockBehavior, LatchType>;

struct DefinitionIdentity
{
    std::string id;
    std::uint32_t version = 1;
    bool operator==(const DefinitionIdentity&) const = default;
};

struct PinDefinition
{
    std::string id;
    std::string label;
    PinType direction = PinType::INPUT;
    unsigned int index = 0;
    GridCoords anchor{0, 0};
    std::vector<GridCoords> lead;
    bool operator==(const PinDefinition&) const = default;
};

struct DefinitionLayout
{
    float width = 0, height = 0;
    std::vector<PinDefinition> pins;
    bool operator==(const DefinitionLayout&) const = default;
};

enum class PresentationKind
{
    NativeSdf,
    Box
};
enum class PinLayoutRule
{
    Fixed,
    SymmetricGateInputs
};

struct ShaderResources
{
    std::string key, vertexPath, fragmentPath;
};

struct PresentationDefinition
{
    PresentationKind kind = PresentationKind::Box;
    ShaderResources shader;
    std::string bodyLabel;
    bool showPinLabels = false;
    bool allowResize = true;
    BodyStyle body;
};

/** Reusable type metadata. Position and live signal/timer/latch state are instance data. */
struct ComponentDefinition
{
    DefinitionIdentity identity;
    std::string displayName;
    NativeBehavior behavior;
    DefinitionLayout layout;
    PinLayoutRule pinLayoutRule = PinLayoutRule::Fixed;
    PresentationDefinition presentation;
    bool defaultInputState = false;
    float defaultClockFrequency = 1.0f;
    bool defaultClockPaused = false;
    std::vector<PropertyRule> propertyRules;
};

/** Retained design configuration; runtime signals/timer state never rewrite it. */
struct ComponentConfiguration
{
    PropertyValues overrides;
    std::optional<std::vector<PinDefinition>> pinLayout;
    bool operator==(const ComponentConfiguration&) const = default;
};

/** Partial update; reset removes an override rather than copying a default value. */
struct ComponentPropertyPatch
{
    PropertyValues values;
    std::vector<std::string> reset;
    std::optional<std::vector<PinDefinition>> pinLayout;
    bool resetPinLayout = false;
};

/** Explicit instance options; absent fields use catalog defaults. */
struct ComponentOverrides
{
    std::optional<int> inputCount;
    std::optional<bool> inputState;
    std::optional<float> clockFrequency;
    std::optional<bool> clockPaused;
    std::optional<DefinitionLayout> layout;
    PropertyValues properties;
    std::optional<std::vector<PinDefinition>> pinLayout;
};

struct ResolvedComponent
{
    DefinitionIdentity identity;
    NativeBehavior behavior;
    DefinitionLayout layout;
    PresentationDefinition presentation;
    bool inputState;
    float clockFrequency;
    bool clockPaused;
    ComponentConfiguration configuration;
    PropertyValues properties;
};

/** @brief Rejects inconsistent metadata/interfaces and unsupported presentation features. */
void validateDefinition(const ComponentDefinition& definition);
/** @brief Applies options to a copy; validates stable pin identities and native behavior counts. */
ResolvedComponent
resolveDefinition(const ComponentDefinition& definition, const ComponentOverrides& overrides = {});
void validateClockFrequency(float hz);

/** @brief Resolves retained overrides without reading transient simulation state. */
ResolvedComponent resolveConfiguration(
    const ComponentDefinition& definition, const ComponentConfiguration& configuration
);
/** @brief Applies a validated set/reset patch; explicit layout regeneration requires
 * resetPinLayout. */
ResolvedComponent patchConfiguration(
    const ComponentDefinition& definition,
    const ComponentConfiguration& current,
    const ComponentPropertyPatch& patch
);
ComponentOverrides configurationOverrides(const ComponentConfiguration& configuration);
