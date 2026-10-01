#pragma once
#include "Components/PinTypes.h"
#include "Geometry/GridCoords.h"
#include "Simulation/NetTypes.h"

#include <vector>

struct PinAnchor
{
    PinRef pin;
    PinType type;
    GridCoords position;
};

struct ComponentGeometry
{
    int componentId;
    GridCoords origin;
    float centerX, centerY;
    float width, height;
    std::vector<PinAnchor> pins;
};

enum class HitType
{
    NONE,
    COMPONENT_PIN,
    COMPONENT_BODY,
    WIRE_START,
    WIRE_END,
    WIRE_BODY,
    WIRE_JUNCTION
};

struct HitResult
{
    HitType type = HitType::NONE;
    int componentId = -1;
    int pinIndex = -1;
    PinType pinType = PinType::INPUT;
    int wireId = INVALID_WIRE_ID;
};

struct WireJunction
{
    GridCoords position;
    PinState state;
};
