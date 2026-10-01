#pragma once

#include "Components/PinTypes.h"
#include "Geometry/GridCoords.h"

#include <cstdint>

struct PinUI
{
    PinType type;
    uint32_t pin_index;
    PinState state;
    GridCoords relative_pos;
};