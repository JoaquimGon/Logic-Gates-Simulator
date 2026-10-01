#pragma once

#include "Geometry/CanvasCamera.h"

/** @brief Tracks camera movement in screen pixels independently of GLFW. */
class PanGesture
{
  public:
    void begin(glm::vec2 cursor)
    {
        m_previous = cursor;
        m_active = true;
    }

    void update(glm::vec2 cursor, const CanvasCameraFrame& frame, CanvasCamera& camera)
    {
        if (!m_active)
            return;
        const auto previous = frame.windowToWorld(m_previous);
        const auto current = frame.windowToWorld(cursor);
        if (!previous || !current)
            return;
        camera.setCenter(camera.center() + *previous - *current);
        m_previous = cursor;
    }

    void cancel() { m_active = false; }

    bool active() const { return m_active; }

  private:
    bool m_active = false;
    glm::vec2 m_previous{};
};
