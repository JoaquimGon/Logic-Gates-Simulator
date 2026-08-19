#pragma once

#include "GateView.h"
#include "GridSystem.h"

#include <vector>
#include <glm/glm.hpp>


// (Include your headers for GridCoords, GridSystem, and PinState here)

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
    // Logic Connections
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
    PinState getState() const;

    // ==========================================
    // Visual Routing
    // ==========================================
    void setPath(const std::vector<GridCoords>& newPath);
    void addPathNode(GridCoords node);
    const std::vector<GridCoords>& getPath() const;

    // Generates the raw [X, Y, Z, R, G, B, A] array for the Engine's dynamic batching
    std::vector<float> getBatchedVertexData() const;

private:
    WireEndpoint m_source;
    WireEndpoint m_dest;

    PinState m_state;
    std::vector<GridCoords> m_path;

    // Helper to grab the correct color based on the current PinState
    glm::vec4 getColorFromState() const;
};