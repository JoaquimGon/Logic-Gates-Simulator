#include "Wire.h"

#include <algorithm>

Wire::Wire() : m_state(PinState::DISCONNECTED) {}

void Wire::setState(PinState newState)
{
    m_state = newState;
}


void Wire::setPath(const std::vector<GridCoords>& newPath)
{
    m_path = newPath;
}


void Wire::addPathNode(GridCoords node)
{
    m_path.push_back(node);
}


const std::vector<GridCoords>& Wire::getPath() const
{
    return m_path;
}


bool Wire::isPointOnSegment(const GridCoords& p, const GridCoords& a, const GridCoords& b)
{
    int crossProduct = (p.y - a.y) * (b.x - a.x) - (p.x - a.x) * (b.y - a.y);
    if (crossProduct != 0)
        return false;

    int minX = std::min(a.x, b.x), maxX = std::max(a.x, b.x);
    int minY = std::min(a.y, b.y), maxY = std::max(a.y, b.y);

    return (p.x >= minX && p.x <= maxX && p.y >= minY && p.y <= maxY);
}


bool Wire::containsPoint(const GridCoords& point, size_t* segmentIndex) const
{
    if (m_path.size() < 2)
        return false;
    for (size_t i = 0; i < m_path.size() - 1; ++i)
    {
        if (isPointOnSegment(point, m_path[i], m_path[i + 1]))
        {
            if (segmentIndex)
                *segmentIndex = i;
            return true;
        }
    }
    return false;
}


bool Wire::splitAt(const GridCoords& splitPoint, Wire& outWireA, Wire& outWireB) const
{
    if (m_path.size() < 2 || splitPoint == m_path.front() || splitPoint == m_path.back())
    {
        return false;
    }

    size_t segmentIdx = 0;
    if (!containsPoint(splitPoint, &segmentIdx))
        return false;

    std::vector<GridCoords> pathA(m_path.begin(), m_path.begin() + segmentIdx + 1);
    if (pathA.empty() || !(pathA.back() == splitPoint))
    {
        pathA.push_back(splitPoint);
    }

    std::vector<GridCoords> pathB;
    pathB.push_back(splitPoint);
    pathB.insert(pathB.end(), m_path.begin() + segmentIdx + 1, m_path.end());

    outWireA = Wire();
    outWireA.setNet(m_net);
    outWireA.setPath(pathA);
    outWireA.setState(m_state);

    outWireB = Wire();
    outWireB.setNet(m_net);
    outWireB.setPath(pathB);
    outWireB.setState(m_state);

    return true;
}


glm::vec4 Wire::getColorFromState() const
{
    if (m_state == PinState::ON)
        return glm::vec4(0.0f, 1.0f, 0.0f, 1.0f);
    if (m_state == PinState::OFF)
        return glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
    return glm::vec4(0.0f, 0.0f, 1.0f, 1.0f); // DISCONNECTED
}


void Wire::simplifyPath()
{
    if (m_path.size() < 2)
        return;

    std::vector<GridCoords> noDupes;
    noDupes.push_back(m_path[0]);
    for (size_t i = 1; i < m_path.size(); ++i)
    {
        if (!(m_path[i] == noDupes.back()))
        {
            noDupes.push_back(m_path[i]);
        }
    }
    m_path = std::move(noDupes);
    if (m_path.size() < 3)
        return;

    std::vector<GridCoords> simplified;
    simplified.push_back(m_path[0]);

    for (size_t i = 1; i < m_path.size() - 1; ++i)
    {
        const auto& prev = simplified.back();
        const auto& curr = m_path[i];
        const auto& next = m_path[i + 1];

        bool isCollinearH = (prev.y == curr.y && curr.y == next.y);
        bool isCollinearV = (prev.x == curr.x && curr.x == next.x);

        if (!isCollinearH && !isCollinearV)
        {
            simplified.push_back(curr);
        }
    }
    simplified.push_back(m_path.back());
    m_path = std::move(simplified);
}


bool Wire::getSegmentAt(const GridCoords& point, GridCoords& outStart, GridCoords& outEnd) const
{
    if (m_path.size() < 2)
        return false;
    for (size_t i = 0; i < m_path.size() - 1; ++i)
    {
        if (isPointOnSegment(point, m_path[i], m_path[i + 1]))
        {
            outStart = m_path[i];
            outEnd = m_path[i + 1];
            return true;
        }
    }
    return false;
}


std::vector<float> Wire::getBatchedVertexData() const
{
    std::vector<float> data;
    if (m_path.size() < 2)
        return data;

    glm::vec4 color = getColorFromState();

    // Visual thickness in world space (half-width from the centerline)
    // 0.006f gives a crisp ~4-6 pixel wide wire at standard zoom
    const float halfThick = 0.006f;

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

    for (size_t i = 0; i < m_path.size() - 1; ++i)
    {
        glm::vec2 a = GridSystem::gridToWorld(m_path[i]);
        glm::vec2 b = GridSystem::gridToWorld(m_path[i + 1]);

        glm::vec2 v1, v2, v3, v4;

        if (m_path[i].y == m_path[i + 1].y)
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