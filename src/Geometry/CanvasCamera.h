#pragma once

#include "Geometry/CanvasViewport.h"

#include <glm/glm.hpp>
#include <optional>

/** Derived once from camera/viewport state; identical matrices serve drawing and picking. */
struct CanvasCameraFrame
{
    CanvasSurface surface;
    CanvasViewport viewport;
    PixelViewport framebufferViewport;
    glm::mat4 viewProjection{1}, inverseViewProjection{1};
    float pixelsPerWorldUnit = 0;

    bool valid() const { return framebufferViewport.width > 0 && framebufferViewport.height > 0; }

    /** Conversions permit points outside the viewport; input ownership checks contains separately.
     */
    std::optional<glm::vec2> windowToWorld(glm::dvec2 point) const;
    std::optional<glm::dvec2> worldToWindow(glm::vec2 point) const;
};

/** UI-independent camera state. Resolves logical layout into clipped framebuffer pixels. */
class CanvasCamera
{
  public:
    glm::vec2 center() const { return m_center; }

    float zoom() const { return m_zoom; }

    const std::optional<CanvasViewport>& viewport() const { return m_viewport; }

    /** Invalid values throw before changing state. Zoom must be finite and positive. */
    void setCenter(glm::vec2 center);
    void setZoom(float zoom);
    void setViewport(std::optional<CanvasViewport> viewport);
    /** @return An inactive frame for empty/minimized surfaces or fully clipped viewports. */
    CanvasCameraFrame frame(CanvasSurface surface) const;

  private:
    glm::vec2 m_center{0};
    float m_zoom = 1;
    std::optional<CanvasViewport> m_viewport;
};
