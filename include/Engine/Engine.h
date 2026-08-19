#pragma once

#include "..\Logic\Circuit.h"
#include "Input.h"
#include "GateView.h"
#include "GridSystem.h"
#include "Wire.h"
#include "..\Graphics\Renderer.h" // Include your new Renderer

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <string>
#include <vector>
#include <memory>

class Engine
{
private:
    bool isDragging = false;
    GLFWwindow* window;
    std::string m_windowName;
    int m_windowWidth = 0;
    int m_windowHeight = 0;

    Input input;

    // NEW: The Renderer now owns all meshes, shaders, and OpenGL state
    Renderer m_renderer;

    static void resizeWindow(GLFWwindow* window, int width, int height)
    {
        glViewport(0, 0, width, height);
    }

public:
    Engine(std::string windowName, int windowWidth, int windowHeight);
    int init();
    void run();

    // Note: If you implement these later, great. Otherwise they can be removed 
    // since glfwSet...Callback directly uses Input::mouseButtonCallback in the cpp.
    static void mouseButtonCallback(GLFWwindow* window, int button, int action, int mods);
    static void cursorPositionCallback(GLFWwindow* window, double xpos, double ypos);
};