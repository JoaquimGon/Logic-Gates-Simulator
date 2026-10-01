#pragma once

#include "Editor/Actions/EditTypes.h"
#include "Geometry/Wire.h"

#include <optional>

class Scene;
struct HitResult;

/** @brief Owns provisional routing and deferred branches; produces one AddWire on release. */
class WireGesture
{
  public:
    void begin(GridCoords position, PinRef origin = {}, PinType direction = PinType::INPUT);
    void beginBranch(WireId wireId, GridCoords position);
    void update(Scene& scene, GridCoords position);
    std::optional<AddWire> finish(const HitResult& hit);
    void cancel();

    bool active() const { return m_active; }

    bool ownsPointer() const { return m_active || m_branch != INVALID_WIRE_ID; }

    const Wire& preview() const { return m_wire; }

    PinRef origin() const { return m_origin; }

    PinType direction() const { return m_direction; }

  private:
    Wire m_wire;
    PinRef m_origin;
    PinType m_direction = PinType::INPUT;
    int m_originComponent = -1;
    GridCoords m_start{};
    WireId m_branch = INVALID_WIRE_ID;
    bool m_active = false;
    bool m_axisLocked = false;
    bool m_xFirst = true;
};
