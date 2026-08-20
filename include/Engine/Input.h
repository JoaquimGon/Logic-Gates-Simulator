#pragma once
#include "GridSystem.h"
#include "GateView.h"
#include "Wire.h"
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

class Input
{
private:
    float m_zoom = 1.0f;
    bool isDragging = false;
    double lastMouseX = 0.0f;
    double lastMouseY = 0.0f;
    GridCoords mouseGridCoords = { 0,0 };
    int currentMouseY = 0;
    glm::vec2 panOffset = glm::vec2(0.0f, 0.0f);

    std::vector<GateView>* m_gates = nullptr;
    GateView* m_draggedGate = nullptr;
    bool isDraggingGate = false;

    // Wire control
    std::vector<Wire>* m_wires = nullptr;
    Wire activeWire;
    std::vector<GridCoords> baseWirePath;
    bool isDrawingWire = false;
    GridCoords wireStartPos = { 0, 0 };
    bool wireAxisLocked = false;
    bool wireAxisXFirst = true;
    std::vector<WireEvent> m_wireEvents;

    int hoveredGateId = -1;
    int hoveredPinIndex = -1;
    PinType hoveredPinType; // <--- ADD THIS LINE
    int hoveredWireIndex = -1;
    bool isHoveredWireStart = false; // True if hovering the Start, False if hovering the End

public:
    void process(GLFWwindow* window);
    static void mouseButtonCallback(GLFWwindow* window, int button, int action, int mods);
    static void cursorPositionCallback(GLFWwindow* window, double xpos, double ypos);
    static void scrollCallback(GLFWwindow* window, double xoffset, double yoffset);

    void handleMouseButton(GLFWwindow* window, int button, int action, int mods);
    void handleCursorPos(GLFWwindow* window, double xpos, double ypos);
    void handleScroll(GLFWwindow* window, double xoffset, double yoffset);

    // NEW: Runs every frame to check what the mouse is over
    void updateHoverState(GLFWwindow* window);

    glm::vec2 getMouseWorldCoord(GLFWwindow* window, float zoom);
    glm::vec2 getPanOffset() const { return panOffset; }
    glm::vec2 getLastMouse() const { return glm::vec2(lastMouseX, lastMouseY); }
    GridCoords getCurrentGridCoords() const { return GridCoords{ 0, 0 }; }
    float getZoom() const { return m_zoom; }

    void setZoom(float zoom) { m_zoom = zoom; }
    void setGates(std::vector<GateView>* gates) { m_gates = gates; }
    void setWires(std::vector<Wire>* wires) { m_wires = wires; }

    bool isCurrentlyDrawingWire() const { return isDrawingWire; }
    Wire getActiveWire() const { return activeWire; }

    std::vector<WireEvent> consumeWireEvents() {
        std::vector<WireEvent> events = m_wireEvents;
        m_wireEvents.clear();
        return events;
    }

    // Getters for the Renderer to draw visual cues later!
    int getHoveredGateId() const { return hoveredGateId; }
    int getHoveredPinIndex() const { return hoveredPinIndex; }
    int getHoveredWireIndex() const { return hoveredWireIndex; }
};