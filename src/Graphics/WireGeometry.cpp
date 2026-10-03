#include "WireGeometry.h"

#include "Geometry/GridSystem.h"
#include "Geometry/Wire.h"

#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>
#include <numbers>

namespace
{
glm::vec4 wireColor(PinState state)
{
    if (state == PinState::ON)
        return glm::vec4(0.0f, 1.0f, 0.0f, 1.0f);
    if (state == PinState::OFF)
        return glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
    return glm::vec4(0.0f, 0.0f, 1.0f, 1.0f); // DISCONNECTED
}
} // namespace

std::vector<float> buildWireVertices(const Wire& wire)
{
    const auto& path = wire.getPath();
    std::vector<float> data;
    if (path.size() < 2)
        return data;

    glm::vec4 color = wireColor(wire.getState());

    // Visual thickness in world space (half-width from the centerline)
    // 0.006f gives a crisp ~4-6 pixel wide wire at standard zoom
    const float halfThick = WireHalfWidth;

    auto pushVertex = [&](glm::vec2 pos)
    {
        data.push_back(pos.x);
        data.push_back(pos.y);
        data.push_back(0.0f);
        data.push_back(color.r);
        data.push_back(color.g);
        data.push_back(color.b);
        data.push_back(color.a);
    };

    for (size_t i = 0; i < path.size() - 1; ++i)
    {
        glm::vec2 a = GridSystem::gridToWorld(path[i]);
        glm::vec2 b = GridSystem::gridToWorld(path[i + 1]);

        glm::vec2 v1, v2, v3, v4;

        if (path[i].y == path[i + 1].y)
        {
            // Horizontal segment: expand Y up/down, slightly extend X to cover
            // corners
            float minX = std::min(a.x, b.x) - halfThick;
            float maxX = std::max(a.x, b.x) + halfThick;
            float minY = a.y - halfThick;
            float maxY = a.y + halfThick;

            v1 = {minX, minY};
            v2 = {maxX, minY};
            v3 = {maxX, maxY};
            v4 = {minX, maxY};
        }
        else
        {
            // Vertical segment: expand X left/right, slightly extend Y to cover
            // corners
            float minX = a.x - halfThick;
            float maxX = a.x + halfThick;
            float minY = std::min(a.y, b.y) - halfThick;
            float maxY = std::max(a.y, b.y) + halfThick;

            v1 = {minX, minY};
            v2 = {maxX, minY};
            v3 = {maxX, maxY};
            v4 = {minX, maxY};
        }

        // Triangle 1 (v1 -> v2 -> v3)
        pushVertex(v1);
        pushVertex(v2);
        pushVertex(v3);

        // Triangle 2 (v1 -> v3 -> v4)
        pushVertex(v1);
        pushVertex(v3);
        pushVertex(v4);
    }

    return data;
}

std::vector<float> buildBoundsVertices(BodyBounds bounds, float padding, float alpha)
{
    bounds.left -= padding;
    bounds.bottom -= padding;
    bounds.right += padding;
    bounds.top += padding;
    if (bounds.width() <= 0 || bounds.height() <= 0)
        return {};
    const float halfStroke =
        std::min({WireHalfWidth * 0.5f, bounds.width() * 0.25f, bounds.height() * 0.25f});
    const float radius = std::min({0.015f, bounds.width() * 0.5f, bounds.height() * 0.5f});
    std::vector<std::pair<glm::vec2, glm::vec2>> ring;
    const glm::vec2 centers[] = {
        {bounds.right - radius, bounds.top - radius},
        {bounds.left + radius, bounds.top - radius},
        {bounds.left + radius, bounds.bottom + radius},
        {bounds.right - radius, bounds.bottom + radius}
    };
    for (int corner = 0; corner < 4; ++corner)
        for (int step = 0; step <= 8; ++step)
        {
            const float angle = (corner + step / 8.0f) * std::numbers::pi_v<float> * 0.5f;
            const glm::vec2 direction{std::cos(angle), std::sin(angle)};
            ring.emplace_back(
                centers[corner] + direction * (radius - halfStroke),
                centers[corner] + direction * (radius + halfStroke)
            );
        }
    std::vector<float> vertices;
    auto vertex = [&](glm::vec2 p)
    { vertices.insert(vertices.end(), {p.x, p.y, 0, 1, 159.0f / 255, 28.0f / 255, alpha}); };
    for (std::size_t i = 0; i < ring.size(); ++i)
    {
        const auto& a = ring[i];
        const auto& b = ring[(i + 1) % ring.size()];
        vertex(a.first);
        vertex(a.second);
        vertex(b.second);
        vertex(a.first);
        vertex(b.second);
        vertex(b.first);
    }
    return vertices;
}
