#pragma once

#include "Editor/Scene.h"

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

class Input;

/** Independent editing scenes; no subcircuit packaging or simulation links yet. */
class CircuitViews
{
  public:
    CircuitViews();

    std::size_t size() const { return m_views.size(); }

    std::size_t activeIndex() const { return m_active; }

    Scene& activeScene() { return *m_views[m_active].scene; }

    const Scene& activeScene() const { return *m_views[m_active].scene; }

    Scene& mainScene() { return *m_views[0].scene; }

    const std::string& name(std::size_t index) const { return m_views.at(index).name; }

    /** Adds an empty scene and switches to it, cancelling unfinished editor gestures. */
    void create(Input& input);
    /** Saves the outgoing camera and restores the destination camera and scene. */
    bool select(std::size_t index, Input& input);
    /** Removes a circuit; active removal switches left before freeing its scene. Main stays. */
    bool remove(std::size_t index, Input& input);
    void rename(std::size_t index, std::string name);

  private:
    struct View
    {
        std::string name;
        std::unique_ptr<Scene> scene = std::make_unique<Scene>();
        glm::vec2 pan{0};
        float zoom = 1;
    };

    std::vector<View> m_views;
    std::size_t m_active = 0;
};
