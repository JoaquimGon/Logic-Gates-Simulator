#include "..\..\include\Engine\Wire.h"
#include <algorithm>

Wire::Wire()
    : m_state(PinState::DISCONNECTED)
{
}

void Wire::setSource(int gateId, int pinIndex) {
    m_source.gateId = gateId;
    m_source.pinIndex = pinIndex;
}

void Wire::setDest(int gateId, int pinIndex) {
    m_dest.gateId = gateId;
    m_dest.pinIndex = pinIndex;
}

void Wire::disconnectSource() {
    m_source.disconnect();
    m_state = PinState::DISCONNECTED;
}

void Wire::disconnectDest() {
    m_dest.disconnect();
}

bool Wire::hasSource() const { return m_source.isConnected(); }
bool Wire::hasDest() const { return m_dest.isConnected(); }

void Wire::setState(PinState newState) {
    m_state = newState;
}

void Wire::setPath(const std::vector<GridCoords>& newPath) {
    m_path = newPath;
}

void Wire::addPathNode(GridCoords node) {
    m_path.push_back(node);
}

const std::vector<GridCoords>& Wire::getPath() const {
    return m_path;
}

bool Wire::isPointOnSegment(const GridCoords& p, const GridCoords& a, const GridCoords& b) {
    // Check collinearity using cross-product for 2D grid points
    int crossProduct = (p.y - a.y) * (b.x - a.x) - (p.x - a.x) * (b.y - a.y);
    if (crossProduct != 0) return false;

    // Check bounding box bounds
    int minX = std::min(a.x, b.x);
    int maxX = std::max(a.x, b.x);
    int minY = std::min(a.y, b.y);
    int maxY = std::max(a.y, b.y);

    return (p.x >= minX && p.x <= maxX && p.y >= minY && p.y <= maxY);
}

bool Wire::containsPoint(const GridCoords& point, size_t* segmentIndex) const {
    if (m_path.size() < 2) return false;

    for (size_t i = 0; i < m_path.size() - 1; ++i) {
        if (isPointOnSegment(point, m_path[i], m_path[i + 1])) {
            if (segmentIndex) *segmentIndex = i;
            return true;
        }
    }
    return false;
}

bool Wire::splitAt(const GridCoords& splitPoint, Wire& outWireA, Wire& outWireB) const {
    size_t segmentIdx = 0;
    if (!containsPoint(splitPoint, &segmentIdx)) {
        return false;
    }

    // Build first segment path (up to split point)
    std::vector<GridCoords> pathA;
    for (size_t i = 0; i <= segmentIdx; ++i) {
        pathA.push_back(m_path[i]);
    }
    if (pathA.empty() || pathA.back() != splitPoint) {
        pathA.push_back(splitPoint);
    }

    // Build second segment path (from split point onward)
    std::vector<GridCoords> pathB;
    pathB.push_back(splitPoint);
    for (size_t i = segmentIdx + 1; i < m_path.size(); ++i) {
        pathB.push_back(m_path[i]);
    }

    // First half (from source up to intersection)
    outWireA = Wire();
    outWireA.setPath(pathA);
    outWireA.setState(m_state);
    if (hasSource()) outWireA.setSource(m_source.gateId, m_source.pinIndex);

    // Second half (from intersection forward to destination)
    outWireB = Wire();
    outWireB.setPath(pathB);
    outWireB.setState(m_state);
    if (hasSource()) outWireB.setSource(m_source.gateId, m_source.pinIndex); // <-- FIX: Inherit source so it stays powered!
    if (hasDest()) outWireB.setDest(m_dest.gateId, m_dest.pinIndex);

    return true;
}

glm::vec4 Wire::getColorFromState() const {
    if (m_state == PinState::ON) {
        return glm::vec4(0.0f, 1.0f, 0.0f, 1.0f); // Green
    }
    else if (m_state == PinState::OFF) {
        return glm::vec4(1.0f, 0.0f, 0.0f, 1.0f); // Red
    }
    return glm::vec4(0.0f, 0.0f, 1.0f, 1.0f);     // Blue 
}

std::vector<float> Wire::getBatchedVertexData() const {
    std::vector<float> data;

    if (m_path.size() < 2) return data;

    glm::vec4 color = getColorFromState();

    for (size_t i = 0; i < m_path.size() - 1; ++i) {
        glm::vec2 startPos = GridSystem::gridToWorld(m_path[i]);
        glm::vec2 endPos = GridSystem::gridToWorld(m_path[i + 1]);

        data.push_back(startPos.x); data.push_back(startPos.y); data.push_back(0.0f);
        data.push_back(color.r); data.push_back(color.g); data.push_back(color.b); data.push_back(color.a);

        data.push_back(endPos.x); data.push_back(endPos.y); data.push_back(0.0f);
        data.push_back(color.r); data.push_back(color.g); data.push_back(color.b); data.push_back(color.a);
    }

    return data;
}