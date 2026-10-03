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

/** Small component palette and read-only information popup, using rectangles and text. */
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

    int infoComponentId() const { return m_infoComponent; }

    /** @brief Reads the current name and synchronized pin states; empty if the component is gone.
     */
    std::vector<std::string> componentInfo(const Scene& scene) const;
    CanvasViewport infoBounds(const Scene& scene) const;

  private:
    const Button* buttonAt(glm::dvec2 point) const;
    std::optional<GridCoords> dropPosition(const CanvasCameraFrame& camera) const;
    void closeInfo();
    int infoVisibleRows(const Scene& scene) const;

    static constexpr double infoRowHeight = 22;
    static constexpr double infoHeaderHeight = 42;
    static constexpr double infoFooterHeight = 30;

    CanvasSurface m_surface{};
    CanvasViewport m_panel{}, m_list{};
    std::vector<Button> m_buttons;
    glm::dvec2 m_pointer{0};
    std::string m_dragDefinition, m_message;
    double m_scroll = 0, m_maxScroll = 0;
    bool m_canCreate = true;
    int m_infoComponent = -1, m_infoScroll = 0;
    glm::dvec2 m_infoAnchor{0};
    bool m_infoRightPressed = false, m_infoEscapePressed = false;
};
