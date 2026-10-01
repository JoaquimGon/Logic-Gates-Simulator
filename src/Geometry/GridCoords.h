#pragma once

struct GridCoords
{
    int x;
    int y;

    bool operator==(const GridCoords& other) const { return x == other.x && y == other.y; }
};
