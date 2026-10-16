#pragma once

#include "Geometry/GridCoords.h"

#include <array>
#include <cstdint>
#include <limits>
#include <stdexcept>

// Counterclockwise quarter turns from the authored, right-facing gate layout.
enum class Orientation
{
    Right,
    Up,
    Left,
    Down
};

inline const char* orientationName(Orientation orientation)
{
    switch (orientation)
    {
    case Orientation::Right:
        return "Right";
    case Orientation::Up:
        return "Up";
    case Orientation::Left:
        return "Left";
    case Orientation::Down:
        return "Down";
    }
    throw std::invalid_argument("Unknown gate orientation.");
}

inline std::array<float, 2> rotatePoint(float x, float y, Orientation orientation)
{
    switch (orientation)
    {
    case Orientation::Right:
        return {x, y};
    case Orientation::Up:
        return {-y, x};
    case Orientation::Left:
        return {-x, -y};
    case Orientation::Down:
        return {y, -x};
    }
    throw std::invalid_argument("Unknown gate orientation.");
}

/** @brief Rotate integer grid offsets with checked arithmetic so extreme coordinates cannot overflow. */
inline GridCoords rotateGridPoint(GridCoords point, Orientation orientation)
{
    std::int64_t x = point.x, y = point.y;
    switch (orientation)
    {
    case Orientation::Right:
        break;
    case Orientation::Up:
        x = -std::int64_t(point.y);
        y = point.x;
        break;
    case Orientation::Left:
        x = -x;
        y = -y;
        break;
    case Orientation::Down:
        x = point.y;
        y = -std::int64_t(point.x);
        break;
    default:
        throw std::invalid_argument("Unknown gate orientation.");
    }
    for (const auto value : {x, y})
        if (value < std::numeric_limits<int>::min() || value > std::numeric_limits<int>::max())
            throw std::invalid_argument("Rotated pin exceeds the grid coordinate range.");
    return {static_cast<int>(x), static_cast<int>(y)};
}
