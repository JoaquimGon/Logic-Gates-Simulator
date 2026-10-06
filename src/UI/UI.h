#pragma once

#include "Editor/UiInput.h"
#include "Geometry/CanvasCamera.h"
#include "Geometry/GridCoords.h"
#include "UI/CircuitTabs.h"

#include <optional>
#include <string>
#include <vector>

class ComponentCatalog;
class Input;
class Renderer;
class Scene;

/** Small tabbed palette and information popup, with a name field for inputs/outputs. */
class UI
{
  public:
    enum class FileCommand
    {
        Save,
        SaveAs,
        Open
    };
    enum class Tab
    {
        Native,
        Custom
    };

    struct Button
    {
        std::string definitionId, label;
        CanvasViewport bounds;
        int inputCount = 0, outputCount = 0;
    };

    void layout(const ComponentCatalog& catalog, CanvasSurface surface, Input& input);
    void setCircuitViews(CircuitViews* views, const FontMetrics* font);
    void update(double now, Input& input);

    const CircuitTabs& circuitTabs() const { return m_circuitTabs; }

    CanvasViewport fileBounds() const;
    CanvasViewport fileMenuBounds() const;
    CanvasViewport fileOptionBounds(int index) const;

    bool fileMenuOpen() const { return m_fileMenuOpen; }

    std::optional<FileCommand> takeFileCommand();
    void setFileStatus(std::string message, bool error = false);
    void dismissPopups(Input& input);

    bool handleInput(
        const UiInputEvent& event, Scene& scene, Input& input, const CanvasCameraFrame& camera
    );
    void cancel(Input& input);
    void draw(Renderer& renderer, const Scene& scene, const CanvasCameraFrame& camera) const;

    const std::vector<Button>& buttons() const { return m_buttons; }

    Tab activeTab() const { return m_tab; }

    CanvasViewport tabBounds(Tab tab) const;

    bool dragging() const { return !m_dragDefinition.empty(); }

    const std::string& message() const { return m_message; }

    int infoComponentId() const { return m_infoComponent; }

    /** @brief Reads the current name and synchronized pin states; empty if the component is gone.
     */
    std::vector<std::string> componentInfo(const Scene& scene) const;
    CanvasViewport infoBounds(const Scene& scene) const;
    CanvasViewport nameBounds(const Scene& scene) const;

  private:
    bool handleFileMenu(const UiInputEvent& event, Input& input);
    void closeFileMenu(Input& input);
    const Button* buttonAt(glm::dvec2 point) const;
    std::optional<GridCoords> dropPosition(const CanvasCameraFrame& camera) const;
    void closeInfo(Input& input);
    bool canName(const Scene& scene) const;
    int infoVisibleRows(const Scene& scene) const;

    static constexpr double infoRowHeight = 22;
    static constexpr double infoHeaderHeight = 42;
    static constexpr double infoFooterHeight = 30;
    static constexpr double infoNameHeight = 64;

    CanvasSurface m_surface{};
    CanvasViewport m_bar{}, m_panel{}, m_list{};
    std::vector<Button> m_buttons;
    Tab m_tab = Tab::Native;
    glm::dvec2 m_pointer{0};
    std::string m_dragDefinition, m_message;
    double m_scroll = 0, m_maxScroll = 0;
    bool m_canCreate = true;
    int m_infoComponent = -1, m_infoScroll = 0;
    glm::dvec2 m_infoAnchor{0};
    bool m_infoRightPressed = false, m_infoEscapePressed = false;
    bool m_nameEditing = false;
    std::string m_nameDraft, m_nameError;
    CircuitTabs m_circuitTabs;
    bool m_fileMenuOpen = false, m_fileMousePressed = false, m_fileEscapePressed = false;
    std::optional<FileCommand> m_fileCommand;
    std::string m_fileStatus;
    bool m_fileError = false;
};
