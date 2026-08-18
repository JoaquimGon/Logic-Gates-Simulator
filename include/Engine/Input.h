#pragma once

#include "GridSystem.h"
#include "GateView.h"

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <iostream>
#include <vector>

class Input
{
private:
    float m_zoom = 1.0f;
    bool isDragging = false;
    double lastMouseX = 0.0f;
    double lastMouseY = 0.0f;
    GridCoords mouseGridCoords = { 0,0 };
    int currentMouseY = 0;
    glm::vec2 panOffset = glm::vec2(0.0f, 0.0f); // Use '=' or '{}' for member initialization

    std::vector<GateView>* m_gates = nullptr;
    GateView* m_draggedGate = nullptr;
    bool isDraggingGate = false;

public:
    void process(GLFWwindow* window);
    void static mouseButtonCallback(GLFWwindow* window, int button, int action, int mods);
    void static cursorPositionCallback(GLFWwindow* window, double xpos, double ypos);

    void handleMouseButton(GLFWwindow* window, int button, int action, int mods);
    void handleCursorPos(GLFWwindow* window, double xpos, double ypos);
    glm::vec2 getMouseWorldCoord(GLFWwindow* window, float zoom);
    
    glm::vec2 getPanOffset() const { return  panOffset; }
    glm::vec2 getLastMouse() const { return glm::vec2(lastMouseX, lastMouseY); }
    GridCoords getCurrentGridCoords() const { return GridCoords{ 0, 0 }; }

    void setZoom(float zoom) { m_zoom = zoom; }
    void setGates(std::vector<GateView>* gates) { m_gates = gates; }
};