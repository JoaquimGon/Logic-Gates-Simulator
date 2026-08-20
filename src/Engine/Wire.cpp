#include "..\..\include\Engine\Wire.h"

Wire::Wire()
    : m_state(PinState::DISCONNECTED)
{
}

// FIX: Just connect the IDs!
void Wire::setSource(int gateId, int pinIndex) {
    m_source.gateId = gateId;
    m_source.pinIndex = pinIndex;
}

// FIX: Just connect the IDs!
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