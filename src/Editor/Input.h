#pragma once
#include "Actions/EditHistory.h"
#include "Actions/EditTypes.h"
#include "Components/PinTypes.h"
#include "Editor/UiInput.h"
#include "EditorMode.h"
#include "Geometry/CanvasCamera.h"
#include "Geometry/GridCoords.h"
#include "Geometry/Wire.h"
#include "Gestures/DragGesture.h"
#include "Gestures/PanGesture.h"
#include "Gestures/Selection.h"
#include "Gestures/WireGesture.h"
#include "Simulation/NetTypes.h"

#include <glm/glm.hpp>
#include <optional>
#include <string>
#include <unordered_set>

struct GLFWwindow;
class Scene;

class Input
{
  private:
    Scene* m_scene = nullptr;
    EditHistory m_localHistory;
    EditHistory* m_history = &m_localHistory;
    UiInputHandler m_uiInputHandler;
    UiInputCapture m_uiCapture;
    CanvasCamera m_camera;
    std::optional<CanvasSurface> m_lastSurface;
    static CanvasSurface surfaceFor(GLFWwindow* window);
    void synchronizeSurface(GLFWwindow* window);
    bool m_canvasFocused = true;
    bool m_windowFocused = true;
    std::unordered_set<int> m_pressedMouseButtons;
    std::unordered_set<int> m_canvasMouseButtons;
    bool dispatchUi(const UiInputEvent& event);
    bool canvasKeyboardAvailable() const;
    bool containsCanvasPoint(GLFWwindow* window, double x, double y) const;
    void cancelGestures();
    void interruptCanvas();
    void clearHover();

    EditorMode m_mode = EditorMode::Selection;
    DragGesture m_drag;
    WireGesture m_wire;
    std::optional<GridCoords> m_committedWirePoint;
    PanGesture m_pan;
    Selection m_selection;

    double lastMouseX = 0.0f;
    double lastMouseY = 0.0f;
    GridCoords mouseGridCoords = {0, 0};

    EditError m_lastEditError = EditError::None;
    std::string m_lastEditMessage;
    bool applyEdit(EditOperation operation);
    void beginSelectionDrag(GridCoords pointer);

    int hoveredComponentId = -1;
    int hoveredPinComponentId = -1;
    int hoveredPinIndex = -1;
    PinType hoveredPinType = PinType::INPUT;
    WireId hoveredWireId = INVALID_WIRE_ID;

    bool m_hoveredSegmentValid = false;
    GridCoords m_hoveredSegmentStart = {0, 0};
    GridCoords m_hoveredSegmentEnd = {0, 0};

    // Keys whose GLFW_PRESS event has arrived but has not been drained by
    // process() yet.
    std::unordered_set<int> m_pendingKeyPresses;
    std::unordered_set<int> m_pressedKeys;

  public:
    /** @brief Installs the UI-first event consumer; changing it discards unfinished canvas input.
     */
    void setUiInputHandler(UiInputHandler handler);
    /** @brief Updates persistent UI ownership; gaining capture cancels previews without
     * deselecting. */
    void setUiCapture(UiInputCapture capture);

    UiInputCapture getUiCapture() const { return m_uiCapture; }

    void setCanvasFocused(bool focused);

    bool isCanvasFocused() const { return m_canvasFocused; }

    /** @brief Sets the shared logical canvas viewport; nullopt uses the full window, zero
     * dimensions disable it. */
    void setCanvasViewport(std::optional<CanvasViewport> bounds);

    const std::optional<CanvasViewport>& getCanvasViewport() const { return m_camera.viewport(); }

    bool isCanvasPointerAvailable(GLFWwindow* window) const;
    void process(GLFWwindow* window);
    static void mouseButtonCallback(GLFWwindow* window, int button, int action, int mods);
    static void cursorPositionCallback(GLFWwindow* window, double xpos, double ypos);
    static void scrollCallback(GLFWwindow* window, double xoffset, double yoffset);
    static void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);

    void handleMouseButton(GLFWwindow* window, int button, int action, int mods);
    void handleKey(int key, int action, int mods = 0, int scanCode = 0);
    static void charCallback(GLFWwindow* window, unsigned int codepoint);
    void handleText(std::uint32_t codepoint);
    void handleFocus(bool focused);
    static void focusCallback(GLFWwindow* window, int focused);

    EditorMode getMode() const { return m_mode; }

    /** @brief Changes mode immediately, discarding unfinished gestures and queued shortcuts. */
    void setMode(EditorMode mode);

    /*
     * @brief Drains the recorded press edge of a key.
     * @param key A GLFW key code.
     * @return true exactly once per physical press, so auto-repeat cannot fire
     * twice.
     */
    bool consumeKeyPress(int key);
    void handleCursorPos(GLFWwindow* window, double xpos, double ypos);
    void handleScroll(GLFWwindow* window, double xoffset, double yoffset);

    void updateHoverState(GLFWwindow* window);
    void cancelCurrentAction();

    bool isIdle() const
    {
        return !m_drag.active() && !m_wire.ownsPointer() && !m_pan.active() &&
               !m_selection.boxing();
    }

    glm::vec2 getMouseWorldCoord(GLFWwindow* window) const;
    CanvasCameraFrame getCameraFrame(GLFWwindow* window) const;

    glm::vec2 getPanOffset() const { return m_camera.center(); }

    void setPanOffset(glm::vec2 offset);

    glm::vec2 getLastMouse() const
    {
        return glm::vec2(static_cast<float>(lastMouseX), static_cast<float>(lastMouseY));
    }

    GridCoords getCurrentGridCoords() const { return mouseGridCoords; }

    float getZoom() const { return m_camera.zoom(); }

    void setZoom(float zoom);

    bool isCurrentlyDrawingWire() const { return m_wire.active(); }

    std::optional<GridCoords> getWireStartPoint() const { return m_wire.startPoint(); }

    /** Hide the cursor guide at a newly committed endpoint until the pointer leaves that cell. */
    bool shouldShowGridPointHighlight() const { return !m_committedWirePoint.has_value(); }

    const Wire& getActiveWire() const { return m_wire.preview(); }

    PinRef getWireOriginPin() const { return m_wire.origin(); }

    PinType getWireOriginType() const { return m_wire.direction(); }

    void setScene(Scene* scene, EditHistory* history = nullptr);
    /** Records successful user edits, including palette/property actions made by the UI. */
    void recordEdit(const EditResult& result);

    std::size_t getUndoCount() const { return m_history->undoCount(); }

    std::size_t getRedoCount() const { return m_history->redoCount(); }

    EditError getLastEditError() const { return m_lastEditError; }

    const std::string& getLastEditMessage() const { return m_lastEditMessage; }

    int getHoveredComponentId() const { return hoveredComponentId; }

    int getHoveredPinIndex() const { return hoveredPinIndex; }

    WireId getHoveredWireId() const { return hoveredWireId; }

    int getHoveredPinComponentId() const { return hoveredPinComponentId; }

    PinType getHoveredPinType() const { return hoveredPinType; }

    const std::set<int>& getSelectedComponents() const { return m_selection.components(); }

    const std::set<WireId>& getSelectedWires() const { return m_selection.wires(); }

    std::optional<BodyBounds> getSelectionBox() const { return m_selection.boxBounds(); }

    int getSelectedComponentId() const { return m_selection.component(); }

    WireId getSelectedWireId() const { return m_selection.wire(); }

    bool hasSelectedSegment() const { return m_selection.segment().has_value(); }

    GridCoords getSelectedSegmentStart() const
    {
        return m_selection.segment() ? m_selection.segment()->start : GridCoords{};
    }

    GridCoords getSelectedSegmentEnd() const
    {
        return m_selection.segment() ? m_selection.segment()->end : GridCoords{};
    }

    bool hasHoveredSegment() const { return m_hoveredSegmentValid; }

    GridCoords getHoveredSegmentStart() const { return m_hoveredSegmentStart; }

    GridCoords getHoveredSegmentEnd() const { return m_hoveredSegmentEnd; }
};
