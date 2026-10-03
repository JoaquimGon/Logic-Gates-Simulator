#pragma once

#include "Editor/UiInput.h"
#include "Geometry/CanvasCamera.h"
#include "Geometry/GridCoords.h"

#include <optional>
#include <string>
#include <vector>

class ComponentCatalog;
class Input;
class Renderer;
class Scene;

/** Small component palette: rectangular buttons, scrolling, and one drag-to-create gesture. */
class UI
{
  public:
    struct Button
    {
        std::string definitionId, label;
        CanvasViewport bounds;
    };

    void layout(const ComponentCatalog& catalog, CanvasSurface surface, Input& input);
    bool handleInput(
        const UiInputEvent& event, Scene& scene, Input& input, const CanvasCameraFrame& camera
    );
    void cancel(Input& input);
    void draw(Renderer& renderer, const Scene& scene, const CanvasCameraFrame& camera) const;

    const std::vector<Button>& buttons() const { return m_buttons; }

    bool dragging() const { return !m_dragDefinition.empty(); }

    const std::string& message() const { return m_message; }

  private:
    const Button* buttonAt(glm::dvec2 point) const;
    std::optional<GridCoords> dropPosition(const CanvasCameraFrame& camera) const;

    CanvasSurface m_surface{};
    CanvasViewport m_panel{}, m_list{};
    std::vector<Button> m_buttons;
    glm::dvec2 m_pointer{0};
    std::string m_dragDefinition, m_message;
    double m_scroll = 0, m_maxScroll = 0;
    bool m_canCreate = true;
};
