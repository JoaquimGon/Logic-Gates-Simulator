#pragma once

#include "..\Graphics\Renderer.h"
#include "..\Logic\Circuit.h"
#include "..\Logic\Wire.h"
#include "..\Views\ComponentView.h"
#include "..\Views\GateView.h"
#include "..\Views\GridSystem.h"
#include "..\Views\InputPinView.h"
#include "Input.h"
#include "Scene.h"

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

class Engine
{
  private:
    GLFWwindow* window = nullptr; // starts null so shutdown() is safe before init()
    std::string m_windowName;
    int m_windowWidth = 0;
    int m_windowHeight = 0;
    bool m_glfwInitialized = false;

    // Last simulation status printed to the console, so a persistent condition
    // (e.g. a combinational loop) is reported on the transition only, not once
    // per frame.
    EvalOrderResult m_lastOrderResult = EvalOrderResult::OK;

    Input input;

    // NEW: The Renderer now owns all meshes, shaders, and OpenGL state
    Renderer m_renderer;

    static void resizeWindow(GLFWwindow* window, int width, int height)
    {
        glViewport(0, 0, width, height);
    }

    /**
    @brief GLFW error sink, registered before glfwInit().
    */
    static void errorCallback(int error, const char* description)
    {
        std::cerr << "[GLFW] Error " << error << ": " << description << std::endl;
    }

  public:
    Engine(std::string windowName, int windowWidth, int windowHeight);
    ~Engine();

    int init();
    void run();

    /*
    @brief Releases the renderer's GL objects, the window and GLFW, in that order.
    */
    void shutdown();
};