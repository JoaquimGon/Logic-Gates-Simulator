#pragma once

#include "GridSystem.h"

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <string>
#include <utility>
#include <vector>

enum PinState {
    DISCONNECTED, // Blue
    OFF,          // Red
    ON,           // Green
};

/*
*   @brief Info for the visual of a pin
*   @param pin_index Same index of the logic side
*   @param state State of the pin
*   @param relative_pos Offset relative to the gate body
* 
*/
struct PinUI {
    uint32_t pin_index;       // 0 for Input A, 1 for Input B, etc.
    PinState state;
    GridCoords relative_pos;  // Offset from gate origin in grid units (e.g. {-1, 1}, {-1, -1}, {2, 0})
};

class GateView
{
public:
    GateView(GridCoords gridPos, glm::vec2 size, std::string shaderName, std::vector<PinUI> pins)
      : m_grid_pos(gridPos), // Initialize grid_pos so it's not garbage!
        m_position(GridSystem::gridToWorld(gridPos)), // Compute world pos automatically
        m_size(size),
        m_shaderName(std::move(shaderName)),
        m_UIPins(std::move(pins))
    {
    }

    std::vector<PinUI> m_UIPins;  // Local pins
    int logic_id;


    glm::vec2 getPosition() const { return m_position; }

    glm::vec2 getSize()     const { return m_size; }
    const std::string& getShaderName() const { return m_shaderName; }

    // Compute absolute grid position of a specific pin
    GridCoords getAbsolutePinGridPos(const PinUI& pin) const {
        return { m_grid_pos.x + pin.relative_pos.x, m_grid_pos.y + pin.relative_pos.y };
    }

    // Compute world position for OpenGL rendering / hit testing
    glm::vec2 getAbsolutePinWorldPos(const PinUI& pin) const {
        return GridSystem::gridToWorld(getAbsolutePinGridPos(pin));
    }

    void setGridPosition(GridCoords newGridPos) {
        m_grid_pos = newGridPos;
        // Keep the floating-point world position perfectly in sync!
        m_position = GridSystem::gridToWorld(newGridPos);
    }

private:
    glm::vec2   m_position; // world-space center, same units as uGridSpacing
    glm::vec2   m_size;     // world-space width/height
    std::string m_shaderName;

    GridCoords m_grid_pos;      // Gate root position (e.g. {10, 5})
};