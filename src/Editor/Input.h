#pragma once
#include "Actions/EditTypes.h"
#include "Components/PinTypes.h"
#include "EditorMode.h"
#include "Geometry/GridCoords.h"
#include "Geometry/Wire.h"
#include "Gestures/DragGesture.h"
#include "Gestures/PanGesture.h"
#include "Gestures/Selection.h"
#include "Gestures/WireGesture.h"
#include "Simulation/NetTypes.h"

#include <glm/glm.hpp>
#include <string>
#include <unordered_set>

struct GLFWwindow;
class Scene;

class Input
{
  private:
    Scene* m_scene = nullptr;

    float m_zoom = 1.0f;
    EditorMode m_mode = EditorMode::Selection;
    DragGesture m_drag;
    WireGesture m_wire;
    PanGesture m_pan;
    Selection m_selection;

    double lastMouseX = 0.0f;
    double lastMouseY = 0.0f;
    GridCoords mouseGridCoords = {0, 0};
    glm::vec2 panOffset = glm::vec2(0.0f, 0.0f);

    EditError m_lastEditError = EditError::None;
    std::string m_lastEditMessage;
    bool applyEdit(EditOperation operation);
    void recordEdit(const EditResult& result);

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
    void process(GLFWwindow* window);
    static void mouseButtonCallback(GLFWwindow* window, int button, int action, int mods);
    static void cursorPositionCallback(GLFWwindow* window, double xpos, double ypos);
    static void scrollCallback(GLFWwindow* window, double xoffset, double yoffset);
    static void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);

    void handleMouseButton(GLFWwindow* window, int button, int action, int mods);
    void handleKey(int key, int action, int mods = 0);
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

    bool isIdle() const { return !m_drag.active() && !m_wire.ownsPointer() && !m_pan.active(); }

    glm::vec2 getMouseWorldCoord(GLFWwindow* window, float zoom) const;

    glm::vec2 getPanOffset() const { return panOffset; }

    glm::vec2 getLastMouse() const
    {
        return glm::vec2(static_cast<float>(lastMouseX), static_cast<float>(lastMouseY));
    }

    GridCoords getCurrentGridCoords() const { return mouseGridCoords; }

    float getZoom() const { return m_zoom; }

    void setZoom(float zoom) { m_zoom = zoom; }

    bool isCurrentlyDrawingWire() const { return m_wire.active(); }

    const Wire& getActiveWire() const { return m_wire.preview(); }

    PinRef getWireOriginPin() const { return m_wire.origin(); }

    PinType getWireOriginType() const { return m_wire.direction(); }

    void setScene(Scene* scene);

    EditError getLastEditError() const { return m_lastEditError; }

    const std::string& getLastEditMessage() const { return m_lastEditMessage; }

    int getHoveredComponentId() const { return hoveredComponentId; }

    int getHoveredPinIndex() const { return hoveredPinIndex; }

    WireId getHoveredWireId() const { return hoveredWireId; }

    int getHoveredPinComponentId() const { return hoveredPinComponentId; }

    PinType getHoveredPinType() const { return hoveredPinType; }

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