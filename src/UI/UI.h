#pragma once

#include "Editor/CircuitViews.h"
#include "Editor/EditorMode.h"
#include "Editor/UiInput.h"
#include "Geometry/CanvasCamera.h"
#include "Geometry/GridCoords.h"
#include "UI/CircuitTabs.h"

#include <optional>
#include <string>
#include <vector>

class ComponentCatalog;
class Gate;
class Input;
class Renderer;
class Scene;

/** Small tabbed palette and information popup, with direct label and logic-gate settings. */
class UI
{
  public:
    enum class FileCommand
    {
        Save,
        SaveAs,
        Open,
        LoadSubcircuit
    };
    enum class Tab
    {
        Native,
        Custom
    };
    enum class BottomTab
    {
        Subcircuit,
        Messages
    };

    enum class MessageKind
    {
        Info,
        Warning,
        Error
    };

    void addMessage(std::string message, MessageKind kind = MessageKind::Error);
    void reportSimulation(const Scene& scene, double now);
    const std::vector<std::string>& messages() const;
    bool hasUnreadMessages() const;
    std::size_t messageCount() const;
    bool hasMessageErrors() const;
    std::string messagesTabLabel() const;
    std::string messageNoticeText() const;
    CanvasViewport messageNoticeBounds() const;
    const std::vector<CircuitViews::SimulationIssue>& simulationIssues() const;
    CanvasViewport findMessageBounds(std::size_t issue) const;
    void clearMessages();
    CanvasViewport messageBoxBounds() const;

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
    std::optional<std::string> takeEditSubcircuit();
    std::vector<std::string> fileOptions() const;
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

    CanvasViewport bottomBounds() const { return m_bottom; }

    CanvasViewport modeBarBounds() const { return m_modes; }

    CanvasViewport modeButtonBounds(EditorMode mode) const;
    std::optional<EditorMode> hoveredMode() const;
    CanvasViewport modeTooltipBounds() const;

    CanvasViewport bottomTabBounds(BottomTab tab) const;
    CanvasViewport subcircuitListBounds() const;
    /** Interface readiness and vital names, including clocks inside saved nested components. */
    std::vector<std::string> subcircuitInfo(const Scene& scene) const;

    BottomTab activeBottomTab() const { return m_bottomTab; }

    bool dragging() const { return !m_dragDefinition.empty(); }

    const std::string& message() const { return m_message; }

    int infoComponentId() const { return m_infoComponent; }

    /** @brief Reads the current name and synchronized pin states; empty if the component is gone.
     */
    std::vector<std::string> componentInfo(const Scene& scene) const;
    CanvasViewport infoBounds(const Scene& scene) const;
    CanvasViewport nameBounds(const Scene& scene) const;
    CanvasViewport editSubcircuitBounds(const Scene& scene) const;
    CanvasViewport gateSettingsBounds(const Scene& scene) const;
    CanvasViewport gateInputButtonBounds(const Scene& scene, bool increase) const;
    CanvasViewport gateInversionBounds(const Scene& scene) const;

  private:
    bool handleFileMenu(const UiInputEvent& event, Input& input);
    void closeFileMenu(Input& input);
    const Button* buttonAt(glm::dvec2 point) const;
    std::optional<GridCoords> dropPosition(const CanvasCameraFrame& camera) const;
    void closeInfo(Input& input);
    bool canName(const Scene& scene) const;
    const Gate* infoGate(const Scene& scene) const;
    bool canResizeGate(const Scene& scene) const;
    bool canInvertGate(const Scene& scene) const;
    bool canEditSubcircuit(const Scene& scene) const;
    int infoVisibleRows(const Scene& scene) const;

    std::string unsavedNoticeText() const;
    double unsavedNoticeWidth() const;
    double navigationStatusWidth() const;
    CircuitViews::Messages& messageState();
    const CircuitViews::Messages& messageState() const;

    struct MessageRow
    {
        std::string text;
        MessageKind kind;
        std::optional<std::size_t> issue;
    };

    std::vector<MessageRow> messageRows(const FontMetrics* font) const;

    static constexpr double infoRowHeight = 22;
    static constexpr double infoHeaderHeight = 42;
    static constexpr double infoFooterHeight = 30;
    static constexpr double infoNameHeight = 64;
    static constexpr double infoGateHeight = 64;

    CanvasSurface m_surface{};
    CanvasViewport m_bar{}, m_panel{}, m_list{}, m_bottom{}, m_modes{};
    std::vector<Button> m_buttons;
    Tab m_tab = Tab::Native;
    BottomTab m_bottomTab = BottomTab::Messages;
    bool m_showSubcircuit = false;
    int m_subcircuitScroll = 0;
    std::size_t m_overviewView = 0;
    glm::dvec2 m_pointer{0};
    std::string m_dragDefinition, m_message;
    bool m_routingError = false;
    double m_scroll = 0, m_maxScroll = 0;
    bool m_canCreate = true;
    int m_infoComponent = -1, m_infoScroll = 0;
    glm::dvec2 m_infoAnchor{0};
    bool m_infoRightPressed = false, m_infoLeftPressed = false, m_infoEscapePressed = false;
    bool m_nameEditing = false;
    int m_nameCommittedKey = -1;
    std::string m_nameDraft, m_infoError;
    CircuitTabs m_circuitTabs;
    bool m_fileMenuOpen = false, m_fileMousePressed = false, m_fileEscapePressed = false;
    std::optional<FileCommand> m_fileCommand;
    std::optional<std::string> m_editSubcircuit;
    CircuitViews* m_views = nullptr;
    const FontMetrics* m_font = nullptr;
    CircuitViews::Messages m_messages;
};
