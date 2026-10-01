#pragma once

#include "Editor/Actions/EditTypes.h"

#include <optional>

class Scene;
struct HitResult;

struct SelectedSegment
{
    GridCoords start;
    GridCoords end;
};

/** @brief Owns selected identities and resolves deletion without editing geometry itself. */
class Selection
{
  public:
    void clear();
    bool selectComponent(int id);
    void selectPinOwner(int id);
    void selectWire(const Scene& scene, WireId id, GridCoords position);

    void clearSegment() { m_segment.reset(); }

    std::optional<EditOperation>
    deletion(const HitResult& hover, std::optional<SelectedSegment> hoveredSegment) const;

    int component() const { return m_component; }

    WireId wire() const { return m_wire; }

    const std::optional<SelectedSegment>& segment() const { return m_segment; }

  private:
    int m_component = -1;
    WireId m_wire = INVALID_WIRE_ID;
    std::optional<SelectedSegment> m_segment;
};
