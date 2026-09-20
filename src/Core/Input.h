#pragma once
#include "..\Views\GridSystem.h"
#include "..\Views\GateView.h"
#include "..\Logic\Wire.h"
#include "Scene.h"

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <iostream>
#include <vector>



enum class WireAction { CONNECT, DISCONNECT };

struct WireEvent {
    WireAction action;
    int srcGateId;
    int srcPinIndex;
    int destGateId;
    int destPinIndex;
};

enum class InteractionState {
    IDLE,
    PANNING,
    DRAGGING_GATE,
    DRAWING_WIRE
};

class Input
{
private:

    Scene* m_scene = nullptr;


    float m_zoom = 1.0f;
    InteractionState m_state = InteractionState::IDLE;

    double lastMouseX = 0.0f;
    double lastMouseY = 0.0f;
    GridCoords mouseGridCoords = { 0, 0 };
    glm::vec2 panOffset = glm::vec2(0.0f, 0.0f);

    ComponentView* m_draggedComponent = nullptr;
    GridCoords m_dragStartPos = { 0, 0 };

    // Wire control
    Wire activeWire;
    int m_wireOriginComponentId = -1;
    std::vector<GridCoords> baseWirePath;
    GridCoords wireStartPos = { 0, 0 };
    bool wireAxisLocked = false;
    bool wireAxisXFirst = true;
    bool isMidWireBranchPending = false; // Deferred split tracking

    int hoveredComponentId = -1;
    int hoveredPinComponentId = -1;
    int hoveredPinIndex = -1;
    PinType hoveredPinType = PinType::INPUT;
    WireId hoveredWireId = INVALID_WIRE_ID; // stable wire id, never a container index
    bool isHoveredWireStart = false;
    bool isHoveredWireEnd = false;

    int m_selectedGateId = -1;
    WireId m_selectedWireId = INVALID_WIRE_ID; // stable wire id, never a container index

    bool m_hoveredSegmentValid = false;
    GridCoords m_hoveredSegmentStart = { 0, 0 };
    GridCoords m_hoveredSegmentEnd = { 0, 0 };

    GridCoords m_selectedSegmentStart = { 0, 0 };
    GridCoords m_selectedSegmentEnd = { 0, 0 };
    bool m_hasSelectedSegment = false;
    int m_selectedComponentId = -1;
public:
    void process(GLFWwindow* window);
    static void mouseButtonCallback(GLFWwindow* window, int button, int action, int mods);
    static void cursorPositionCallback(GLFWwindow* window, double xpos, double ypos);
    static void scrollCallback(GLFWwindow* window, double xoffset, double yoffset);

    void handleMouseButton(GLFWwindow* window, int button, int action, int mods);
    void handleCursorPos(GLFWwindow* window, double xpos, double ypos);
    void handleScroll(GLFWwindow* window, double xoffset, double yoffset);

    void updateHoverState(GLFWwindow* window);
    void cancelCurrentAction();
    bool isIdle() const { return m_state == InteractionState::IDLE; }

    glm::vec2 getMouseWorldCoord(GLFWwindow* window, float zoom);
    glm::vec2 getPanOffset() const { return panOffset; }
    glm::vec2 getLastMouse() const { return glm::vec2(static_cast<float>(lastMouseX), static_cast<float>(lastMouseY)); }
    GridCoords getCurrentGridCoords() const { return mouseGridCoords; }
    float getZoom() const { return m_zoom; }

    void setZoom(float zoom) { m_zoom = zoom; }

    bool isCurrentlyDrawingWire() const { return m_state == InteractionState::DRAWING_WIRE; }
    Wire getActiveWire() const { return activeWire; }

    void setScene(Scene* scene) { m_scene = scene; }

    int getHoveredComponentId() const { return hoveredComponentId; }
    int getHoveredPinIndex() const { return hoveredPinIndex; }
    WireId getHoveredWireId() const { return hoveredWireId; }

    int getHoveredPinComponentId() const { return hoveredPinComponentId; }
    PinType getHoveredPinType() const { return hoveredPinType; }


    int getSelectedComponentId() const { return m_selectedComponentId; }
    WireId getSelectedWireId() const { return m_selectedWireId; }

    bool hasSelectedSegment() const { return m_hasSelectedSegment; }
    GridCoords getSelectedSegmentStart() const { return m_selectedSegmentStart; }
    GridCoords getSelectedSegmentEnd() const { return m_selectedSegmentEnd; }

    bool hasHoveredSegment() const { return m_hoveredSegmentValid; }
    GridCoords getHoveredSegmentStart() const { return m_hoveredSegmentStart; }
    GridCoords getHoveredSegmentEnd() const { return m_hoveredSegmentEnd; }
};