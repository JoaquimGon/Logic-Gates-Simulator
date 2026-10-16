#pragma once

#include "Components/PinTypes.h"
#include "GridCoords.h"
#include "Simulation/NetTypes.h"

#include <cstddef>
#include <vector>

class Wire
{
  public:
    Wire();

    NetId getNet() const { return m_net; }
    void setNet(NetId net) { m_net = net; }

    void setState(PinState newState);

    PinState getState() const { return m_state; }

    void setPath(const std::vector<GridCoords>& newPath);

    void addPathNode(GridCoords node);

    const std::vector<GridCoords>& getPath() const;

    bool containsPoint(const GridCoords& point, size_t* segmentIndex = nullptr) const;

    /** @brief Split a route at an interior point while preserving its electrical identity and path orientation. */
    bool splitAt(const GridCoords& splitPoint, Wire& outWireA, Wire& outWireB) const;

    /** @brief Remove duplicate points and redundant collinear bends without changing the route's covered geometry. */
    void simplifyPath();

    bool getSegmentAt(const GridCoords& point, GridCoords& outStart, GridCoords& outEnd) const;

  private:
    NetId m_net = INVALID_NET_ID;
    PinState m_state = PinState::DISCONNECTED;
    std::vector<GridCoords> m_path;

    static bool isPointOnSegment(const GridCoords& p, const GridCoords& a, const GridCoords& b);
};
