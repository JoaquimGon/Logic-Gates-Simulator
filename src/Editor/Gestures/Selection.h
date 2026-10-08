#pragma once

#include "Editor/Actions/EditTypes.h"

#include <optional>
#include <set>
#include <span>

class Scene;
class Wire;
struct HitResult;

struct SelectedSegment
{
    GridCoords start;
    GridCoords end;
};

/** Selected object IDs, a legacy single-segment selection, and a pending box gesture. */
class Selection
{
  public:
    void clear();
    bool selectComponent(int id);
    void selectPinOwner(int id);
    void selectWire(const Scene& scene, WireId id, GridCoords position);
    void toggleWire(WireId id);
    void prune(const Scene& scene);
    /** Keeps selected geometry selected when normalization replaces its wire IDs. */
    void retainMovedWires(
        const Scene& scene, std::span<const WireId> added, std::span<const Wire> paths
    );
    void beginBox(glm::vec2 point);
    void updateBox(glm::vec2 point);
    void finishBox(const Scene& scene);

    void cancelBox() { m_boxStart.reset(); }

    std::optional<BodyBounds> boxBounds() const;

    bool boxing() const { return m_boxStart.has_value(); }

    bool group() const { return m_components.size() + m_wires.size() > 1; }

    bool containsComponent(int id) const { return m_components.contains(id); }

    bool containsWire(WireId id) const { return m_wires.contains(id); }

    const std::set<int>& components() const { return m_components; }

    const std::set<WireId>& wires() const { return m_wires; }

    void clearSegment() { m_segment.reset(); }

    EditBatch
    deletions(const HitResult& hover, std::optional<SelectedSegment> hoveredSegment) const;

    int component() const { return m_components.empty() ? -1 : *m_components.begin(); }

    WireId wire() const { return m_wires.empty() ? m_segmentWire : *m_wires.begin(); }

    const std::optional<SelectedSegment>& segment() const { return m_segment; }

  private:
    bool m_areaSelected = false;
    std::set<int> m_components;
    std::set<WireId> m_wires;
    WireId m_segmentWire = INVALID_WIRE_ID;
    std::optional<SelectedSegment> m_segment;
    std::optional<glm::vec2> m_boxStart;
    glm::vec2 m_boxEnd{};
};
