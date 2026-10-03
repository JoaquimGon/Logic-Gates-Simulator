#pragma once

#include "Components/Definitions/ComponentDefinition.h"
#include "Components/Gate.h"
#include "Components/Latch.h"
#include "Components/Views/ComponentLayout.h"
#include "Geometry/GridCoords.h"
#include "Simulation/NetTypes.h"

#include <cstdint>
#include <glm/glm.hpp>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

class Scene;

enum class PlacementPolicy
{
    RejectOverlap,
    FindFree,
    AllowOverlap
};
enum class RemovedPinPolicy
{
    RejectAttached,
    LeaveWires
};

struct RegisterComponentDefinition
{
    ComponentDefinition definition;
};

struct CreateComponent
{
    std::string definitionId;
    GridCoords position;
    ComponentOverrides overrides;
    PlacementPolicy placement = PlacementPolicy::RejectOverlap;
    std::uint32_t version = 0;
};

// Compatibility requests adapt explicit legacy geometry into the same catalog/factory path.
struct CreateGate
{
    GateType type;
    GridCoords position;
    ComponentLayout layout;
    PlacementPolicy placement = PlacementPolicy::RejectOverlap;
};

struct CreateInput
{
    GridCoords position;
    glm::vec2 size;
    std::string shader;
    std::optional<bool> state;
    PlacementPolicy placement = PlacementPolicy::RejectOverlap;
};

struct CreateClock
{
    GridCoords position;
    glm::vec2 size;
    std::string shader;
    std::optional<float> frequency;
    PlacementPolicy placement = PlacementPolicy::RejectOverlap;
};

struct CreateLatch
{
    LatchType type;
    GridCoords position;
    PlacementPolicy placement = PlacementPolicy::RejectOverlap;
};

struct MoveComponent
{
    int componentId;
    GridCoords position;
};

struct DeleteComponent
{
    int componentId;
};

/**
 * @brief Updates native layout and gate arity together; other native counts remain fixed.
 * Surviving indices keep identity. Appended inputs start false; routes stay in grid space.
 * Removing a wired input requires an explicit policy or wire removal in the same batch.
 */
struct ConfigureComponent
{
    int componentId;
    ComponentLayout layout;
    RemovedPinPolicy removedPins = RemovedPinPolicy::RejectAttached;
};

/** @brief Edits explicit instance settings; omitted fields retain their current values.
 * Scalable gate counts clamp to 2-8; NOT stays fixed at one. Wired pin removal is rejected.
 */
struct ConfigureComponentProperties
{
    int componentId;
    std::optional<std::string> label;
    std::optional<int> inputCount;
    std::optional<bool> inverted;
    std::optional<float> clockFrequency;
    std::optional<bool> clockPaused;
};

struct ConfigureInput
{
    int componentId;
    bool state;
};

struct ConfigureClock
{
    int componentId;
    float frequency;
    bool paused;
};

struct AddWire
{
    std::vector<GridCoords> path;
};

struct DeleteWire
{
    WireId wireId;
};

struct DeleteWireSegment
{
    WireId wireId;
    GridCoords start;
    GridCoords end;
};

using EditOperation = std::variant<
    RegisterComponentDefinition,
    CreateComponent,
    CreateGate,
    CreateInput,
    CreateClock,
    CreateLatch,
    MoveComponent,
    DeleteComponent,
    ConfigureComponent,
    ConfigureComponentProperties,
    ConfigureInput,
    ConfigureClock,
    AddWire,
    DeleteWire,
    DeleteWireSegment>;
using EditBatch = std::vector<EditOperation>;

enum class EditError
{
    None,
    InvalidComponent,
    InvalidConfiguration,
    InvalidWire,
    InvalidSegment,
    Overlap,
    AttachedPin,
    PreviewActive,
    StaleRevision
};

/**
 * @brief Complete committed images, including normalized geometry and runtime state.
 * Raw pointers obtained from Scene are invalidated by applying/restoring an edit.
 * Wire IDs listed here describe the final normalization result, not provisional inserts.
 */
struct EditRecord
{
    std::shared_ptr<const Scene> before;
    std::shared_ptr<const Scene> after;
    std::vector<WireId> addedWireIds;
    std::vector<WireId> removedWireIds;
};

struct EditResult
{
    EditError error = EditError::None;
    std::string message;
    std::vector<int> createdComponentIds;
    // Compatibility IDs: normalization can replace these. Use change for final identities.
    std::vector<WireId> insertedWireIds;
    std::shared_ptr<const EditRecord> change;

    explicit operator bool() const { return error == EditError::None; }
};

struct MovePreviewHandle
{
    std::uint64_t token = 0;
};
