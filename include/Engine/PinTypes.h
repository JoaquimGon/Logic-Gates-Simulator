#pragma once
#include "GridSystem.h"
#include <cstdint>

enum PinState { DISCONNECTED, OFF, ON };
enum class PinType { INPUT, OUTPUT };

struct PinUI {
    PinType type;
    uint32_t pin_index;
    PinState state;
    GridCoords relative_pos;
};