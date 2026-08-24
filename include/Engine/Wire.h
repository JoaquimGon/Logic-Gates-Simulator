#pragma once

#include "GateView.h"
#include "GridSystem.h"
#include <vector>
#include <glm/glm.hpp>

struct WireEndpoint {
    int componentId = -1;  // -1 means disconnected. The id of whatever component owns this pin.
    int pinIndex = -1;

    bool isConnected() const { return componentId != -1; }
    void disconnect() { componentId = -1; pinIndex = -1; }
};

class Wire {
public:
    Wire();

    // ==========================================
    // Logic Connections
    // ==========================================
    void setSource(int componentId, int pinIndex);
    void setDest(int componentId, int pinIndex);

    void disconnectSource();
    void disconnectDest();

    bool hasSource() const;
    bool hasDest() const;

    WireEndpoint getSource() const { return m_source; }
    WireEndpoint getDest() const { return m_dest; }

    // ==========================================
    // State & Simulation
    // ==========================================
    void setState(PinState newState);
    PinState getState() const { return m_state; }

    // ==========================================
    // Visual Routing & Intersection Geometry
    // ==========================================
    void setPath(const std::vector<GridCoords>& newPath);
    void addPathNode(GridCoords node);
    const std::vector<GridCoords>& getPath() const;

    /**
     * Checks if a given coordinate lies anywhere on any segment of this wire.
     */
    bool containsPoint(const GridCoords& point, size_t* segmentIndex = nullptr) const;

    /**
     * Splits this wire into two separate wires at a given grid coordinate along its path.
     */
    bool splitAt(const GridCoords& splitPoint, Wire& outWireA, Wire& outWireB) const;

    void simplifyPath();
    bool getSegmentAt(const GridCoords& point, GridCoords& outStart, GridCoords& outEnd) const;

    std::vector<float> getBatchedVertexData() const;

private:
    WireEndpoint m_source;
    WireEndpoint m_dest;

    PinState m_state;
    std::vector<GridCoords> m_path;

    glm::vec4 getColorFromState() const;

    static bool isPointOnSegment(const GridCoords& p, const GridCoords& a, const GridCoords& b);
};