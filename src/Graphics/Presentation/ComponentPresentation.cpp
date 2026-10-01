#include "Graphics/Presentation/ComponentPresentation.h"

#include <algorithm>

std::vector<ComponentRenderData>
buildComponentPresentation(const std::unordered_map<int, std::unique_ptr<ComponentView>>& views)
{
    std::vector<ComponentRenderData> result;
    for (const auto& [id, view] : views)
    {
        ComponentRenderData data{
            {id, view->getPosition(), view->getSize(), view->getShaderName(), view->getBodyStyle()},
            view->getBodyLabel(),
            view->showsPinLabels(),
            {}
        };
        for (const auto* pins : {&view->getInputPins(), &view->getOutputPins()})
            for (const auto& pin : *pins)
            {
                std::vector<glm::vec2> waypoints;
                for (const auto point : pin.lead)
                    waypoints.push_back(
                        GridSystem::gridToWorld(
                            {view->getGridPosition().x + point.x,
                             view->getGridPosition().y + point.y}
                        )
                    );
                const auto anchor = view->getAbsolutePinWorldPos(pin);
                data.pins.push_back(
                    {pin.type,
                     pin.pin_index,
                     pin.state,
                     anchor,
                     pin.label,
                     buildPinLead(data.body, anchor, waypoints)}
                );
            }
        std::sort(
            data.pins.begin(),
            data.pins.end(),
            [](const auto& a, const auto& b)
            {
                if (a.direction != b.direction)
                    return a.direction < b.direction;
                return a.index < b.index;
            }
        );
        result.push_back(std::move(data));
    }
    std::sort(
        result.begin(),
        result.end(),
        [](const auto& a, const auto& b) { return a.body.id < b.body.id; }
    );
    return result;
}
