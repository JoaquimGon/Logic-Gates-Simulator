#pragma once
#include "Components/Definitions/PresentationGeometry.h"
#include "Components/Gate.h"
#include "Components/Latch.h"
#include "Components/PinTypes.h"
#include "Geometry/GridCoords.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

struct ManualInputBehavior
{
};

struct ClockBehavior
{
};

struct OutputBehavior
{
};

class Scene;
class Circuit;

/** Authored design and a simulation template; instances clone the latter. */
struct SubcircuitBehavior
{
    std::shared_ptr<const Scene> authored;
    std::shared_ptr<const Circuit> circuit;
    std::vector<int> inputs, outputs;
};

using ComponentBehavior = std::variant<
    GateType,
    ManualInputBehavior,
    ClockBehavior,
    LatchType,
    OutputBehavior,
    SubcircuitBehavior>;

struct DefinitionIdentity
{
    std::string id;
    std::uint32_t version = 1;
};

struct PinDefinition
{
    std::string id;
    std::string label;
    PinType direction = PinType::INPUT;
    unsigned int index = 0;
    GridCoords anchor{0, 0};
    std::vector<GridCoords> lead;
};

struct DefinitionLayout
{
    // Shader scale in world units, independent of integer pin anchors and visible body bounds.
    float width = 0, height = 0;
    std::vector<PinDefinition> pins;
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
    ComponentBehavior behavior;
    DefinitionLayout layout;
    PinLayoutRule pinLayoutRule = PinLayoutRule::Fixed;
    PresentationDefinition presentation;
    bool defaultInputState = false;
    float defaultClockFrequency = 1.0f;
    bool defaultClockPaused = false;
};

/** Explicit instance options; absent fields use catalog defaults. */
struct ComponentOverrides
{
    std::optional<int> inputCount;
    std::optional<bool> inputState;
    std::optional<float> clockFrequency;
    std::optional<bool> clockPaused;
    std::optional<DefinitionLayout> layout;
};

struct ResolvedComponent
{
    DefinitionIdentity identity;
    ComponentBehavior behavior;
    DefinitionLayout layout;
    PresentationDefinition presentation;
    bool inputState;
    float clockFrequency;
    bool clockPaused;
};

/** @brief Rejects inconsistent metadata/interfaces and unsupported presentation features. */
void validateDefinition(const ComponentDefinition& definition);
/** @brief Applies options to a copy; validates stable pin identities and behavior pin counts. */
ResolvedComponent
resolveDefinition(const ComponentDefinition& definition, const ComponentOverrides& overrides = {});
void validateClockFrequency(float hz);
