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
    // Logic Connections
    // ==========================================
    void setSource(int gateId, int pinIndex);
    void setDest(int gateId, int pinIndex);

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
     * @param point The grid coordinate to test.
     * @param segmentIndex Optional out-param receiving which segment index (i to i+1) contains the point.
     * @return True if the point lies along the wire's segments.
     */
    bool containsPoint(const GridCoords& point, size_t* segmentIndex = nullptr) const;

    /**
     * Splits this wire into two separate wires at a given grid coordinate along its path.
     * Keeps the original source on wire1 and sets the original dest on wire2.
     * @param splitPoint The point along the wire where the split occurs.
     * @param outWireA Returns the first half (from source to split point).
     * @param outWireB Returns the second half (from split point to dest).
     * @return True if the split succeeded.
     */
    bool splitAt(const GridCoords& splitPoint, Wire& outWireA, Wire& outWireB) const;

    std::vector<float> getBatchedVertexData() const;

private:
    WireEndpoint m_source;
    WireEndpoint m_dest;

    PinState m_state;
    std::vector<GridCoords> m_path;

    glm::vec4 getColorFromState() const;

    static bool isPointOnSegment(const GridCoords& p, const GridCoords& a, const GridCoords& b);
};