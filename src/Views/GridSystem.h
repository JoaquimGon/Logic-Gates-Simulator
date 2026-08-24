#pragma once

#include <glm/glm.hpp>
#include <cmath>


struct GridCoords {
    int x;
    int y;

    bool operator==(const GridCoords& other) const {
        return x == other.x && y == other.y;
    }
};

class GridSystem {
public:
    static constexpr float GRID_SPACING = 0.05f;

    // Converts continuous world space (e.g. {0.103f, -0.048f}) to discrete grid integer cells ({2, -1})
    static GridCoords worldToGrid(glm::vec2 worldPos) {
        return {
            static_cast<int>(std::round(worldPos.x / GRID_SPACING)),
            static_cast<int>(std::round(worldPos.y / GRID_SPACING))
        };
    }

    // Converts discrete integer grid coords ({2, -1}) back to exact world space coordinates ({0.10f, -0.05f})
    static glm::vec2 gridToWorld(GridCoords gridPos) {
        return {
            static_cast<float>(gridPos.x) * GRID_SPACING,
            static_cast<float>(gridPos.y) * GRID_SPACING
        };
    }

    // Snaps any arbitrary world-space position (e.g. mouse cursor position) to the nearest grid intersection
    static glm::vec2 snapToGrid(glm::vec2 worldPos) {
        return gridToWorld(worldToGrid(worldPos));
    }
};