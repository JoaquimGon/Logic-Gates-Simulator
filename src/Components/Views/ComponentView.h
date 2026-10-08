#pragma once
#include "Components/Definitions/ComponentDefinition.h"
#include "Geometry/GridSystem.h"
#include "PinUI.h"

#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

class Circuit; // forward declare, defined in Simulation/Circuit.h

class EditorActions;
class ComponentFactory;

class ComponentView
{
  public:
    ComponentView(GridCoords gridPos, int logicId, glm::vec2 size, std::string shaderName)
        : m_grid_pos(gridPos), m_logicId(logicId), m_position(GridSystem::gridToWorld(gridPos)),
          m_size(size), m_shaderName(std::move(shaderName))
    {
    }

    virtual ~ComponentView() = default;

    virtual std::unique_ptr<ComponentView> clone() const = 0;

    int getComponentId() const
    {
        return m_logicId;
    } // kept name for drop-in compatibility with existing call sites

    glm::vec2 getPosition() const { return m_position; }

    /** Shader scale in world units; need not be an even number of grid cells. */
    glm::vec2 getSize() const { return m_size; }

    BodyBounds getBodyBounds() const
    {
        auto bounds = bodyBounds(m_bodyStyle, m_position.x, m_position.y, m_size.x, m_size.y);
        if (const auto rail = getInputRail())
        {
            bounds.left = std::min(bounds.left, rail->left);
            bounds.right = std::max(bounds.right, rail->right);
            bounds.bottom = std::min(bounds.bottom, rail->bottom);
            bounds.top = std::max(bounds.top, rail->top);
        }
        if (getInputPins().size() > 2 &&
            (m_bodyStyle.contour == BodyContour::And || m_bodyStyle.contour == BodyContour::Or ||
             m_bodyStyle.contour == BodyContour::Xor))
            for (const auto& pin : getInputPins())
            {
                const auto point = getAbsolutePinWorldPos(pin);
                bounds.left = std::min(bounds.left, point.x);
                bounds.right = std::max(bounds.right, point.x);
                bounds.bottom = std::min(bounds.bottom, point.y);
                bounds.top = std::max(bounds.top, point.y);
            }
        return bounds;
    }

    /** Visual extension for aligned inputs outside a native gate's body; never an electrical net.
     */
    std::optional<BodyBounds> getInputRail() const
    {
        const auto& pins = getInputPins();
        if (pins.size() <= 2 ||
            (m_bodyStyle.contour != BodyContour::And && m_bodyStyle.contour != BodyContour::Or &&
             m_bodyStyle.contour != BodyContour::Xor))
            return std::nullopt;
        const auto first = getAbsolutePinWorldPos(pins.front());
        float bottom = first.y, top = first.y;
        for (const auto& pin : pins)
        {
            // Irregular/explicitly routed interfaces retain their authored presentation.
            if (pin.relative_pos.x != pins.front().relative_pos.x || !pin.lead.empty())
                return std::nullopt;
            const auto point = getAbsolutePinWorldPos(pin);
            bottom = std::min(bottom, point.y);
            top = std::max(top, point.y);
        }
        const auto body = bodyBounds(m_bodyStyle, m_position.x, m_position.y, m_size.x, m_size.y);
        if (bottom >= body.bottom && top <= body.top)
            return std::nullopt;
        constexpr float halfWidth = 0.003f;
        if (m_bodyStyle.contour == BodyContour::Xor)
        {
            const float width = m_bodyStyle.inverted ? m_size.x / 1.5f : m_size.x;
            const float margin = 0.013f * width + halfWidth;
            return BodyBounds{
                m_position.x + (-1.27f + std::sqrt(0.860f * 0.860f - 0.38f * 0.38f)) * width -
                    margin,
                bottom - halfWidth,
                m_position.x - 0.41f * width + margin,
                top + halfWidth
            };
        }
        // The outer edge meets the back/corner of the body; the strip extends inward.
        return BodyBounds{
            body.left, bottom - halfWidth, body.left + 2 * halfWidth, top + halfWidth
        };
    }

    const std::string& getShaderName() const { return m_shaderName; }

    const DefinitionIdentity& getDefinitionIdentity() const { return m_definition; }

    const std::string& getBodyLabel() const { return m_bodyLabel; }

    bool showsPinLabels() const { return m_showPinLabels; }

    const BodyStyle& getBodyStyle() const { return m_bodyStyle; }

    GridCoords getGridPosition() const { return m_grid_pos; }

    GridCoords getAbsolutePinGridPos(const PinUI& pin) const
    {
        return {m_grid_pos.x + pin.relative_pos.x, m_grid_pos.y + pin.relative_pos.y};
    }

    glm::vec2 getAbsolutePinWorldPos(const PinUI& pin) const
    {
        return GridSystem::gridToWorld(getAbsolutePinGridPos(pin));
    }

    // ----- Data-driven pin access every component must provide -----
    virtual const std::vector<PinUI>& getInputPins() const = 0;
    virtual const std::vector<PinUI>& getOutputPins() const = 0;

    // ----- Delegated interaction -----
    // Called when the component's BODY (not a pin) is clicked. Return true if
    // the component consumed the click (e.g. toggled itself) so Input should
    // not start a drag.
    virtual bool onClick(Circuit& /*circuit*/) { return false; }

  protected:
    friend class EditorActions;
    friend class ComponentFactory;
    friend class Scene;

    void setGridPosition(GridCoords newGridPos)
    {
        m_grid_pos = newGridPos;
        m_position = GridSystem::gridToWorld(newGridPos);
    }

    virtual std::vector<PinUI>& editInputPins() = 0;
    virtual std::vector<PinUI>& editOutputPins() = 0;

    DefinitionIdentity m_definition;
    std::string m_bodyLabel;
    bool m_showPinLabels = false;
    BodyStyle m_bodyStyle;
    glm::vec2 m_position;
    glm::vec2 m_size;
    std::string m_shaderName;
    GridCoords m_grid_pos;
    int m_logicId;
};
