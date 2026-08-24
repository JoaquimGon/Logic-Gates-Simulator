#pragma once
#include "..\Logic\PinTypes.h"
#include <glm/glm.hpp>
#include <string>
#include <vector>

class Circuit; // forward declare, defined in Logic/Circuit.h

class ComponentView
{
public:
    ComponentView(GridCoords gridPos, int logicId, glm::vec2 size, std::string shaderName)
        : m_grid_pos(gridPos), m_logicId(logicId),
        m_position(GridSystem::gridToWorld(gridPos)),
        m_size(size), m_shaderName(std::move(shaderName)) {
    }

    virtual ~ComponentView() = default;

    int getComponentId() const { return m_logicId; } // kept name for drop-in compatibility with existing call sites
    glm::vec2 getPosition() const { return m_position; }
    glm::vec2 getSize()     const { return m_size; }
    const std::string& getShaderName() const { return m_shaderName; }

    void setGridPosition(GridCoords newGridPos) {
        m_grid_pos = newGridPos;
        m_position = GridSystem::gridToWorld(newGridPos);
    }
    GridCoords getGridPosition() const { return m_grid_pos; }

    GridCoords getAbsolutePinGridPos(const PinUI& pin) const {
        return { m_grid_pos.x + pin.relative_pos.x, m_grid_pos.y + pin.relative_pos.y };
    }
    glm::vec2 getAbsolutePinWorldPos(const PinUI& pin) const {
        return GridSystem::gridToWorld(getAbsolutePinGridPos(pin));
    }

    // ----- Data-driven pin access every component must provide -----
    virtual std::vector<PinUI>& getInputPins() = 0;
    virtual const std::vector<PinUI>& getInputPins() const = 0;
    virtual std::vector<PinUI>& getOutputPins() = 0;
    virtual const std::vector<PinUI>& getOutputPins() const = 0;

    // ----- Delegated interaction -----
    // Called when the component's BODY (not a pin) is clicked. Return true if the
    // component consumed the click (e.g. toggled itself) so Input should not start a drag.
    virtual bool onClick(Circuit& /*circuit*/) { return false; }

protected:
    glm::vec2   m_position;
    glm::vec2   m_size;
    std::string m_shaderName;
    GridCoords  m_grid_pos;
    int         m_logicId;
};