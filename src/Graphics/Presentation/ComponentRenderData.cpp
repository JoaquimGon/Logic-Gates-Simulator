#include "Graphics/Presentation/ComponentRenderData.h"

#include <algorithm>
#include <cmath>

std::vector<ComponentBatch> buildComponentBatches(std::span<const ComponentRenderData> components)
{
    std::vector<const ComponentBodyInstance*> ordered;
    for (const auto& component : components)
        ordered.push_back(&component.body);
    std::sort(
        ordered.begin(), ordered.end(), [](const auto* a, const auto* b) { return a->id < b->id; }
    );
    std::vector<ComponentBatch> batches;
    for (const auto* instance : ordered)
    {
        if (batches.empty() || batches.back().shader != instance->shader ||
            batches.back().instances.front().drawRearArc != instance->drawRearArc)
            batches.push_back({instance->shader, {}});
        batches.back().instances.push_back(*instance);
    }
    return batches;
}

std::vector<float> packComponentInstances(std::span<const ComponentBodyInstance> instances)
{
    std::vector<float> packed;
    packed.reserve(instances.size() * 9);
    for (const auto& instance : instances)
    {
        packed.insert(
            packed.end(),
            {instance.position.x, instance.position.y, instance.size.x, instance.size.y}
        );
        packed.insert(packed.end(), instance.style.tint.begin(), instance.style.tint.end());
        packed.push_back(static_cast<float>(instance.orientation));
    }
    return packed;
}

std::vector<glm::vec2> buildPinLead(
    const ComponentBodyInstance& body, glm::vec2 anchor, std::span<const glm::vec2> waypoints
)
{
    if (body.orientation != Orientation::Right)
    {
        auto right = body;
        right.orientation = Orientation::Right;
        const auto inverse = static_cast<Orientation>((4 - static_cast<int>(body.orientation)) % 4);
        auto transform = [&](glm::vec2 point, Orientation orientation)
        {
            const auto rotated =
                rotatePoint(point.x - body.position.x, point.y - body.position.y, orientation);
            return body.position + glm::vec2{rotated[0], rotated[1]};
        };
        std::vector<glm::vec2> local;
        for (auto point : waypoints)
            local.push_back(transform(point, inverse));
        auto lead = buildPinLead(right, transform(anchor, inverse), local);
        for (auto& point : lead)
            point = transform(point, body.orientation);
        return lead;
    }
    const glm::vec2 start = waypoints.empty() ? anchor : waypoints.front();
    const auto normalized = (start - body.position) / body.size;
    const auto contact = bodyContact(body.style, normalized.x, normalized.y);
    std::vector<glm::vec2> lead;
    const auto endpoint = body.position + glm::vec2(contact[0], contact[1]) * body.size;
    lead.push_back(endpoint);
    if (std::abs(endpoint.x - start.x) > 0.000001f && std::abs(endpoint.y - start.y) > 0.000001f)
        lead.push_back(
            usesHorizontalContact(body.style, normalized.x, normalized.y)
                ? glm::vec2(start.x, endpoint.y)
                : glm::vec2(endpoint.x, start.y)
        );
    if (waypoints.empty())
        lead.push_back(anchor);
    else
        lead.insert(lead.end(), waypoints.begin(), waypoints.end());
    return lead;
}
