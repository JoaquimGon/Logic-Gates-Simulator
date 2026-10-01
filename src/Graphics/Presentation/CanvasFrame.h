#pragma once
#include "Geometry/Wire.h"
#include "Graphics/Presentation/ComponentRenderData.h"

#include <map>
#include <optional>

struct BodyHighlight
{
    glm::vec2 position, size;
    float opacity = 1;
};

struct SegmentHighlight
{
    GridCoords start, end;
    float opacity = 1;
};

struct GridHighlight
{
    GridCoords position;
    float opacity = 1;
};

/** Borrowed frame values; drawCanvas consumes them synchronously and retains no scene pointers. */
struct CanvasFrame
{
    std::span<const ComponentRenderData> components;
    const std::map<WireId, Wire>& wires;
    std::span<const glm::vec3> junctions; // Typed grid junction integration remains RM-A8.
    const Wire* activeWire = nullptr;
    std::optional<BodyHighlight> bodyHighlight;
    std::optional<SegmentHighlight> segmentHighlight;
    std::optional<GridHighlight> gridHighlight;
    int hoveredComponent = -1;
    int hoveredPin = -1;
    PinType hoveredDirection = PinType::INPUT;
};
