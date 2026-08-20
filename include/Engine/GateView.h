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

// NEW: Explicitly define if a pin is an input or an output
enum class PinType {
    INPUT,
    OUTPUT
};

/*
*   @brief Info for the visual of a pin
*   @param type Whether this pin receives or sends electricity
*   @param pin_index Same index of the logic side
*   @param state State of the pin
*   @param relative_pos Offset relative to the gate body
*
*/
struct PinUI {
    PinType type;             // NEW: Differentiates Input vs Output
    uint32_t pin_index;       // 0 for Input A, 1 for Input B, etc.
    PinState state;
    GridCoords relative_pos;  // Offset from gate origin in grid units
};

class GateView
{
public:
    /*
    *   @brief Visual of a gate constructor
    *   @param gridPos Position of the gate in the grid
    *   @param gateId The gate's id in the logic side
    *   @param size Size of the gate
    *   @param shaderName Name of the gate's shader
    *   @param inPins The input pins
    *   @param outPins The output pins
    */
    GateView(GridCoords gridPos, int gateId, glm::vec2 size, std::string shaderName,
        std::vector<PinUI> inPins, std::vector<PinUI> outPins)
        : m_grid_pos(gridPos),
        logic_id(gateId),
        m_position(GridSystem::gridToWorld(gridPos)),
        m_size(size),
        m_shaderName(std::move(shaderName)),
        m_inputs(std::move(inPins)),
        m_outputs(std::move(outPins))
    {
    }

    // NEW: Separated vectors
    std::vector<PinUI> m_inputs;
    std::vector<PinUI> m_outputs;

    glm::vec2 getPosition() const { return m_position; }
    glm::vec2 getSize()     const { return m_size; }
    const std::string& getShaderName() const { return m_shaderName; }
    int getGateId() const { return logic_id; }

    GridCoords getAbsolutePinGridPos(const PinUI& pin) const {
        return { m_grid_pos.x + pin.relative_pos.x, m_grid_pos.y + pin.relative_pos.y };
    }

    glm::vec2 getAbsolutePinWorldPos(const PinUI& pin) const {
        return GridSystem::gridToWorld(getAbsolutePinGridPos(pin));
    }

    // Updated Getters with boundary safety
    PinUI& getOutputPinUI(int index = 0) {
        if (index < m_outputs.size()) return m_outputs[index];
        return m_outputs.front();
    }

    PinUI& getInputPinUI(int index) {
        if (index < m_inputs.size()) return m_inputs[index];
        return m_inputs.front();
    }

    void setGridPosition(GridCoords newGridPos) {
        m_grid_pos = newGridPos;
        m_position = GridSystem::gridToWorld(newGridPos);
    }

private:
    glm::vec2   m_position;
    glm::vec2   m_size;
    std::string m_shaderName;
    GridCoords  m_grid_pos;
    int         logic_id;
};