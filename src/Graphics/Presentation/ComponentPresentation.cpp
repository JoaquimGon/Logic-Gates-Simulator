#include "Graphics/Presentation/ComponentPresentation.h"

#include "Geometry/GridMetrics.h"

#include <algorithm>
#include <cmath>


std::vector<ComponentRenderData>
buildComponentPresentation(const std::unordered_map<int, std::unique_ptr<ComponentView>>& views)
{
    std::vector<ComponentRenderData> result;
    for (const auto& [id, view] : views)
    {
        ComponentRenderData data{
            {id, view->getPosition(), view->getSize(), view->getShaderName(), view->getBodyStyle()},
            view->showsBodyLabel() ? view->getBodyLabel() : std::string{},
            view->showsPinLabels(),
            {},
            view->getUnrotatedInputRail()
        };
        const bool xorExtension = data.inputRail && data.body.style.contour == BodyContour::Xor;
        const float shapeWidth = data.body.style.inverted ? data.body.size.x / 1.5f : data.body.size.x;
        auto arcX = [&](float y)
        {
            const float span =
                std::max(data.body.position.y - data.inputRail->bottom, data.inputRail->top - data.body.position.y);
            const float localY = std::clamp((y - data.body.position.y) / span, -1.0f, 1.0f) * 0.38f;
            return data.body.position.x + (-1.27f + std::sqrt(0.860f * 0.860f - localY * localY)) * shapeWidth;
        };
        if (xorExtension)
        {
            data.body.drawRearArc = false;
            for (int step = 0; step <= 64; ++step)
            {
                const float y = data.inputRail->bottom + (data.inputRail->top - data.inputRail->bottom) * step / 64;
                data.inputArc.push_back({arcX(y), y});
            }
        }
        if (data.body.style.contour == BodyContour::Output && !view->getInputPins().empty())
        {
            const auto state = view->getInputPins().front().state;
            data.body.style.tint = state == PinState::ON    ? std::array<float, 4>{0, 1, 0, 1}
                                   : state == PinState::OFF ? std::array<float, 4>{0.12f, 0.15f, 0.2f, 1}
                                                            : std::array<float, 4>{0.32f, 0.38f, 0.46f, 1};
        }
        for (const auto* pins : {&view->getInputPins(), &view->getOutputPins()})
            for (const auto& pin : *pins)
            {
                std::vector<glm::vec2> waypoints;
                for (const auto point : pin.lead)
                    waypoints.push_back(
                        view->getOrientation() == Orientation::Right
                            ? GridSystem::gridToWorld(
                                  {view->getGridPosition().x + point.x, view->getGridPosition().y + point.y}
                              )
                            : view->getPosition() + GridSystem::gridToWorld(point)
                    );
                const auto anchor = view->getOrientation() == Orientation::Right
                                        ? view->getAbsolutePinWorldPos(pin)
                                        : view->getPosition() + GridSystem::gridToWorld(pin.relative_pos);
                auto lead = buildPinLead(data.body, anchor, waypoints);
                // Inner rows go straight to the solid gate. Outer rows meet its extension.
                if (data.inputRail && pin.type == PinType::INPUT && waypoints.empty() &&
                    std::abs(anchor.y - data.body.position.y) > 0.38f * data.body.size.y)
                {
                    const float x = xorExtension ? arcX(anchor.y) : data.inputRail->centerX();
                    lead = {{x, anchor.y}, anchor};
                }
                std::optional<glm::vec2> bubble;
                if (pin.inverted)
                {
                    // The added input cell holds the bubble; the original terminal stays
                    // connected to the body/rail and the wire ends at the outer anchor.
                    bubble = anchor + glm::vec2(GridMetrics::Spacing / 2, 0);
                    lead.back() = anchor + glm::vec2(GridMetrics::Spacing, 0);
                }
                data.pins.push_back(
                    {pin.type, pin.pin_index, pin.state, view->getAbsolutePinWorldPos(pin), pin.label, std::move(lead),
                     bubble}
                );
            }
        data.body.orientation = view->getOrientation();
        auto orient = [&](glm::vec2 point)
        {
            if (data.body.orientation == Orientation::Right)
                return point;
            const auto rotated =
                rotatePoint(point.x - data.body.position.x, point.y - data.body.position.y, data.body.orientation);
            return data.body.position + glm::vec2{rotated[0], rotated[1]};
        };
        for (auto& pin : data.pins)
        {
            for (auto& point : pin.lead)
                point = orient(point);
            if (pin.inversionBubble)
            {
                const auto inner = rotatePoint(GridMetrics::Spacing, 0, data.body.orientation);
                const auto center = rotatePoint(GridMetrics::Spacing / 2, 0, data.body.orientation);
                pin.lead.back() = pin.position + glm::vec2{inner[0], inner[1]};
                pin.inversionBubble = pin.position + glm::vec2{center[0], center[1]};
            }
            else
                pin.lead.back() = pin.position;
        }
        for (auto& point : data.inputArc)
            point = orient(point);
        std::sort(
            data.pins.begin(), data.pins.end(),
            [](const auto& a, const auto& b)
            {
                if (a.direction != b.direction)
                    return a.direction < b.direction;
                return a.index < b.index;
            }
        );
        result.push_back(std::move(data));
    }
    std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) { return a.body.id < b.body.id; });
    return result;
}
