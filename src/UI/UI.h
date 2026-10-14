#pragma once

#include "Editor/CircuitViews.h"
#include "Editor/EditorMode.h"
#include "Editor/UiInput.h"
#include "Geometry/CanvasCamera.h"
#include "Geometry/GridCoords.h"
#include "UI/CircuitTabs.h"
#include "UI/TruthTable.h"

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
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
        LoadSubcircuit,
        ExportSubcircuit,
        OpenStorage,
        ChangeStorage
    };
    enum class Menu
    {
        File,
        Edit,
        View,
        Simulation
    };

    struct MenuItem
    {
        std::string label, shortcut;
        bool enabled = true, checked = false;
    };

    CanvasViewport menuBounds(Menu menu) const;
    CanvasViewport dropdownBounds(Menu menu) const;
    CanvasViewport menuOptionBounds(Menu menu, int index) const;
    std::vector<MenuItem> menuOptions(Menu menu) const;

    /** Lets the frame loop present the reset state before advancing automatic clock time. */
    bool takeSimulationReset() { return std::exchange(m_simulationReset, false); }

    bool menuOpen(Menu menu) const { return m_openMenu == menu; }

    bool bottomVisible() const { return m_bottomVisible; }

    bool debugVisible() const { return m_debugVisible; }

    enum class UnsavedChoice
    {
        Save,
        Discard,
        Cancel
    };
    void showUnsavedPrompt(std::vector<std::string> names, bool exiting, Input& input);

    bool unsavedPromptOpen() const { return !m_unsavedNames.empty(); }

    std::optional<UnsavedChoice> takeUnsavedChoice();
    CanvasViewport unsavedPromptBounds() const;
    CanvasViewport unsavedButtonBounds(UnsavedChoice choice) const;

    enum class Tab
    {
        Native,
        Custom
    };
    enum class BottomTab
    {
        Subcircuit,
        Messages,
        Clocks,
        Signals
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
        std::filesystem::path draftPath;
    };

    void setLibraryDrafts(std::vector<Button> drafts) { m_libraryDrafts = std::move(drafts); }

    std::optional<std::filesystem::path> takeOpenDraft();

    std::optional<std::size_t> takeCloseCircuit() { return m_circuitTabs.takeCloseRequest(); }

    void layout(const ComponentCatalog& catalog, CanvasSurface surface, Input& input);
    void setCircuitViews(CircuitViews* views, const FontMetrics* font);
    void update(double now, Input& input);

    const CircuitTabs& circuitTabs() const { return m_circuitTabs; }

    CanvasViewport fileBounds() const;
    CanvasViewport fileMenuBounds() const;
    CanvasViewport fileOptionBounds(int index) const;

    bool fileMenuOpen() const { return menuOpen(Menu::File); }

    std::optional<FileCommand> takeFileCommand();
    std::optional<std::string> takeEditSubcircuit();
    std::vector<std::string> fileOptions() const;
    std::vector<FileCommand> fileCommands() const;
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

    CanvasViewport bottomResizeBounds() const;

    CanvasViewport modeBarBounds() const { return m_modes; }

    CanvasViewport modeButtonBounds(EditorMode mode) const;
    std::optional<EditorMode> hoveredMode() const;
    CanvasViewport modeTooltipBounds() const;

    CanvasViewport bottomTabBounds(BottomTab tab) const;
    enum class ClockControl
    {
        RunPause,
        SingleStep,
        DoubleStep,
        ZoomOut,
        ZoomIn,
        Live,
        Fit,
        Clear
    };
    CanvasViewport clockControlBounds(ClockControl control) const;
    CanvasViewport clockListBounds() const;
    CanvasViewport clockFrequencyBounds(int visibleRow) const;
    CanvasViewport clockPauseBounds(int visibleRow) const;
    CanvasViewport
    signalTraceBounds(const Scene& scene, PinType direction, unsigned int index) const;
    CanvasViewport signalRemoveBounds(int row) const;

    double clockWindowSeconds() const;
    /** Clocks centre live time; Signals retain their history-only frame. */
    double clockStartTime() const;
    /** Display-only scheduled edges, bounded independently of the recorded history. */
    std::deque<ClockSample> clockPreview(const ClockReading& clock) const;
    std::string clockTimeFrameText() const;
    bool clockWaveformTooDense(float frequency) const;
    std::vector<std::string> clockTooltip(const Scene& scene) const;
    CanvasViewport clockTooltipBounds(const Scene& scene) const;

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
    CanvasViewport labelVisibilityBounds(const Scene& scene) const;
    CanvasViewport editSubcircuitBounds(const Scene& scene) const;
    CanvasViewport viewInsideBounds(const Scene& scene) const;
    CanvasViewport inspectionBackBounds() const;
    CanvasViewport gateSettingsBounds(const Scene& scene) const;
    CanvasViewport gateInputButtonBounds(const Scene& scene, bool increase) const;
    CanvasViewport gateInputListBounds(const Scene& scene) const;
    CanvasViewport gateInputInversionBounds(const Scene& scene, unsigned int index) const;
    CanvasViewport componentOrientationBounds(const Scene& scene, Orientation orientation) const;
    CanvasViewport gateInversionBounds(const Scene& scene) const;
    CanvasViewport infoTabBounds(const Scene& scene, bool table) const;

    bool truthTableOpen() const { return m_truthTableOpen; }

    const TruthTable& truthTable(const Scene& scene) const;
    std::optional<int> truthTableCurrentRow(const Scene& scene) const;
    CanvasViewport truthTableRowsBounds(const Scene& scene) const;
    CanvasViewport currentTruthRowBounds(const Scene& scene) const;
    int truthTableVisibleRows(const Scene& scene) const;
    int truthTableScroll(const Scene& scene) const;

  private:
    bool handleUnsavedPrompt(const UiInputEvent& event, Input& input);
    void drawUnsavedPrompt(Renderer& renderer) const;
    void layoutBottom(Input& input);
    bool handleBottomResize(const UiInputEvent& event, Input& input);
    void finishBottomResize(Input& input);
    bool handleMenus(const UiInputEvent& event, Input& input);
    void refreshMenuOptions(const Input& input);
    void executeMenuOption(Menu menu, int index, Input& input);
    bool handleClocks(const UiInputEvent& event, Scene& scene, Input& input);
    void closeClockEdit(Input& input);
    void drawClocks(Renderer& renderer, const Scene& scene) const;
    std::optional<SignalTrace>
    signalForPin(const Scene& scene, PinType direction, unsigned int index) const;
    const PinUI* infoPinAtLine(const Scene& scene, int line) const;
    void drawSignalRows(Renderer& renderer) const;
    double fittedSignalWindow() const;
    void drawClockTooltip(Renderer& renderer, const Scene& scene) const;
    void updateClockHover(double now);
    static double fittedClockWindow(const std::vector<ClockReading>& readings);
    ClockTimeline& clockTimeline();
    const ClockTimeline& clockTimeline() const;
    double clockLabelWidth() const;
    int clockScroll(const Scene& scene) const;
    static std::string frequencyText(float frequency);
    void closeMenu(Input& input);
    const Button* buttonAt(glm::dvec2 point) const;
    std::optional<GridCoords> dropPosition(const CanvasCameraFrame& camera) const;
    void closeInfo(Input& input);
    bool hasTruthTable(const Scene& scene) const;
    double infoHeaderSize(const Scene& scene) const;
    void revealCurrentTruthRow(const Scene& scene);
    void drawTruthTable(Renderer& renderer, const Scene& scene) const;
    bool canName(const Scene& scene) const;
    const Gate* infoGate(const Scene& scene) const;
    bool canResizeGate(const Scene& scene) const;
    bool canInvertGate(const Scene& scene) const;
    bool canEditSubcircuit(const Scene& scene) const;
    bool canViewInside(const Scene& scene) const;
    double infoActionHeight(const Scene& scene) const;
    bool canOrient(const Scene& scene) const;
    CanvasViewport orientationSettingsBounds(const Scene& scene) const;
    double infoSettingsHeight(const Scene& scene) const;
    int infoVisibleRows(const Scene& scene) const;
    int gateInputRows(const Scene& scene) const;
    int gateInputScroll(const Scene& scene) const;

    double navigationStatusX() const;
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
    static constexpr double infoLabelHeight = 30;
    static constexpr double infoGateHeight = 64;
    static constexpr double infoOrientationHeight = 50;

    std::vector<std::string> m_unsavedNames;
    std::optional<UnsavedChoice> m_unsavedChoice, m_unsavedPressed;
    bool m_unsavedExiting = false;
    int m_unsavedScroll = 0;
    CanvasSurface m_surface{};
    CanvasViewport m_bar{}, m_panel{}, m_list{}, m_bottom{}, m_modes{};
    CanvasViewport m_inspectionBar{};
    bool m_readOnly = false;
    std::optional<double> m_bottomHeight;
    bool m_resizingBottom = false, m_bottomResizePressed = false;
    double m_bottomResizeY = 0, m_bottomResizeHeight = 0;
    std::vector<Button> m_buttons, m_libraryDrafts;
    std::optional<std::filesystem::path> m_openDraft;
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
    bool m_truthTableOpen = false;
    int m_truthScroll = 0;
    mutable int m_truthComponent = -1;
    mutable std::uint64_t m_truthRevision = 0;
    mutable TruthTable m_truthTable;
    glm::dvec2 m_infoAnchor{0};
    bool m_infoRightPressed = false, m_infoLeftPressed = false, m_infoEscapePressed = false;
    bool m_nameEditing = false;
    int m_nameCommittedKey = -1;
    std::string m_nameDraft, m_infoError;
    CircuitTabs m_circuitTabs;
    std::optional<Menu> m_openMenu;
    std::optional<std::pair<Menu, int>> m_menuCommand;
    std::vector<MenuItem> m_editOptions, m_viewOptions;
    bool m_bottomVisible = true, m_debugVisible = false, m_simulationReset = false;
    int m_menuShortcutKey = -1;
    bool m_fileMousePressed = false, m_fileEscapePressed = false;
    std::optional<FileCommand> m_fileCommand;
    std::optional<std::string> m_editSubcircuit;
    CircuitViews* m_views = nullptr;
    const FontMetrics* m_font = nullptr;
    CircuitViews::Messages m_messages;
    ClockTimeline m_clockTimeline;
    int m_clockScroll = 0;
    double m_clockWindow = 2, m_signalWindow = 2, m_clockEnd = 2;
    bool m_clockFollow = true, m_clockFitPending = true;
    int m_clockHover = -1;
    double m_clockHoverSince = 0, m_clockHoverNow = 0;
    int m_clockEditing = -1, m_clockCommittedKey = -1;
    bool m_clockReplaceDraft = false, m_clockMousePressed = false;
    std::string m_clockDraft, m_clockError;
};
