#pragma once

#include "Components/PinTypes.h"
#include "Geometry/GridCoords.h"

#include <cstdint>
#include <string>

struct PinUI
{
    PinType type;
    uint32_t pin_index;
    PinState state;
    GridCoords relative_pos;
    std::string id = {};
    std::string label = {};
};