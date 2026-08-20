#pragma once

#include "GateView.h"
#include "GridSystem.h"
#include <vector>
#include <glm/glm.hpp>

struct WireEndpoint {
    int gateId = -1;       // -1 means disconnected
    int pinIndex = -1;

    bool isConnected() const { return gateId != -1; }
    void disconnect() { gateId = -1; pinIndex = -1; }
};

class Wire {
public:
    Wire();

    // ==========================================
    // Logic Connections (No State Needed Here!)
    // ==========================================
    void setSource(int gateId, int pinIndex);
    void setDest(int gateId, int pinIndex);

    void disconnectSource();
    void disconnectDest();

    bool hasSource() const;
    bool hasDest() const;

    // ==========================================
    // State & Simulation
    // ==========================================
    void setState(PinState newState);
    PinState getState() const { return m_state; }

    WireEndpoint getSource() const { return m_source; }
    WireEndpoint getDest() const { return m_dest; }

    // ==========================================
    // Visual Routing
    // ==========================================
    void setPath(const std::vector<GridCoords>& newPath);
    void addPathNode(GridCoords node);
    const std::vector<GridCoords>& getPath() const;

    std::vector<float> getBatchedVertexData() const;

private:
    WireEndpoint m_source;
    WireEndpoint m_dest;

    PinState m_state;
    std::vector<GridCoords> m_path;

    glm::vec4 getColorFromState() const;
};