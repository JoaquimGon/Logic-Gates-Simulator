#pragma once
#include "Components/Definitions/ComponentDefinition.h"
#include "Geometry/GridMetrics.h"
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

    Orientation getOrientation() const { return m_orientation; }

    BodyBounds getBodyBounds() const
    {
        return orientedBounds(getUnrotatedBodyBounds(), m_position.x, m_position.y, m_orientation);
    }

    /** Body-frame bounds and rail, before the instance's orientation is applied. */
    BodyBounds getUnrotatedBodyBounds() const
    {
        auto bounds = bodyBounds(m_bodyStyle, m_position.x, m_position.y, m_size.x, m_size.y);
        if (const auto rail = getUnrotatedInputRail())
        {
            bounds.left = std::min(bounds.left, rail->left);
            bounds.right = std::max(bounds.right, rail->right);
            bounds.bottom = std::min(bounds.bottom, rail->bottom);
            bounds.top = std::max(bounds.top, rail->top);
        }
        const bool invertedInput = std::any_of(
            getInputPins().begin(),
            getInputPins().end(),
            [](const auto& pin) { return pin.inverted; }
        );
        if (invertedInput ||
            (getInputPins().size() > 2 &&
             (m_bodyStyle.contour == BodyContour::And || m_bodyStyle.contour == BodyContour::Or ||
              m_bodyStyle.contour == BodyContour::Xor)))
            for (const auto& pin : getInputPins())
            {
                const auto point = m_position + GridSystem::gridToWorld(pin.relative_pos);
                bounds.left = std::min(bounds.left, point.x);
                bounds.right = std::max(bounds.right, point.x);
                const float radius = pin.inverted ? GridMetrics::Spacing / 2 : 0;
                bounds.bottom = std::min(bounds.bottom, point.y - radius);
                bounds.top = std::max(bounds.top, point.y + radius);
            }
        return bounds;
    }

    /** Visual extension for aligned inputs outside a native gate's body; never an electrical net.
     */
    std::optional<BodyBounds> getInputRail() const
    {
        const auto rail = getUnrotatedInputRail();
        return rail
                   ? std::optional{orientedBounds(*rail, m_position.x, m_position.y, m_orientation)}
                   : std::nullopt;
    }

    std::optional<BodyBounds> getUnrotatedInputRail() const
    {
        const auto& pins = getInputPins();
        if (pins.size() <= 2 ||
            (m_bodyStyle.contour != BodyContour::And && m_bodyStyle.contour != BodyContour::Or &&
             m_bodyStyle.contour != BodyContour::Xor))
            return std::nullopt;
        const auto first = m_position + GridSystem::gridToWorld(pins.front().relative_pos);
        float bottom = first.y, top = first.y;
        for (const auto& pin : pins)
        {
            // Irregular/explicitly routed interfaces retain their authored presentation.
            if (pin.relative_pos.x != pins.front().relative_pos.x || !pin.lead.empty())
                return std::nullopt;
            const auto point = m_position + GridSystem::gridToWorld(pin.relative_pos);
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

    /** Stable identity of an authored input/output, independent of its name or pin order. */
    const std::string& getPortId() const { return m_portId; }

    bool showsBodyLabel() const { return m_showBodyLabel; }

    bool showsPinLabels() const { return m_showPinLabels; }

    const BodyStyle& getBodyStyle() const { return m_bodyStyle; }

    GridCoords getGridPosition() const { return m_grid_pos; }

    GridCoords getAbsolutePinGridPos(const PinUI& pin) const
    {
        const auto point = rotateGridPoint(pin.relative_pos, m_orientation);
        return {m_grid_pos.x + point.x, m_grid_pos.y + point.y};
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
    std::string m_portId;
    bool m_showPinLabels = false;
    bool m_showBodyLabel = true;
    BodyStyle m_bodyStyle;
    Orientation m_orientation = Orientation::Right;
    glm::vec2 m_position;
    glm::vec2 m_size;
    std::string m_shaderName;
    GridCoords m_grid_pos;
    int m_logicId;
};
