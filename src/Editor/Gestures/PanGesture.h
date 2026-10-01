#pragma once

#include <glm/glm.hpp>

/** @brief Tracks camera movement in screen pixels independently of GLFW. */
class PanGesture
{
  public:
    void begin(glm::vec2 cursor)
    {
        m_previous = cursor;
        m_active = true;
    }

    void update(glm::vec2 cursor, float viewportHeight, float zoom, glm::vec2& offset)
    {
        if (!m_active || viewportHeight <= 0 || zoom <= 0)
            return;
        const auto delta = (cursor - m_previous) * (2.0f / viewportHeight / zoom);
        offset += glm::vec2{-delta.x, delta.y};
        m_previous = cursor;
    }

    void cancel() { m_active = false; }

    bool active() const { return m_active; }

  private:
    bool m_active = false;
    glm::vec2 m_previous{};
};
