#pragma once

#include "Editor/Actions/EditTypes.h"

#include <optional>

class Scene;

/** @brief Owns one component move preview until release or cancellation. */
class DragGesture
{
  public:
    bool begin(Scene& scene, int componentId);
    bool update(Scene& scene, GridCoords position);
    EditResult finish(Scene& scene, bool reroute = true);
    void cancel(Scene& scene);

    bool active() const { return m_preview.has_value(); }

  private:
    std::optional<MovePreviewHandle> m_preview;
};
