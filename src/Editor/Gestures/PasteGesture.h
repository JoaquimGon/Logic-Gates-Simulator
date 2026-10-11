#pragma once

#include "Editor/Clipboard.h"
#include "Geometry/GeometryTypes.h"

#include <memory>
#include <optional>
#include <set>

class Scene;

/** Copied design held outside the real scene until an explicit placement click. */
class PasteGesture
{
  public:
    PasteGesture();
    ~PasteGesture();
    EditResult
    begin(const Scene& destination, const EditorClipboard& clipboard, GridCoords position);
    void update(const Scene& destination, GridCoords position);
    EditResult finish(Scene& destination);
    void cancel();

    bool active() const { return m_scene != nullptr; }

    const Scene* scene() const { return m_scene.get(); }

    const std::set<int>& blockedComponents() const { return m_blocked; }

    const std::set<int>& overlappingComponents() const { return m_overlapping; }

  private:
    EditorClipboard m_copy;
    std::unique_ptr<Scene> m_scene;
    std::optional<MovePreviewHandle> m_move;
    std::vector<ComponentGeometry> m_geometry;
    GridCoords m_origin{}, m_position{};
    std::optional<std::uint64_t> m_destinationRevision;
    std::set<int> m_overlapping, m_blocked;
    bool m_positionValid = false;
};
