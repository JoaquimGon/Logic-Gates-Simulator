#pragma once

#include "Editor/Input.h"
#include "Graphics/Renderer.h"
#include "Simulation/Circuit.h"

#include <string>

struct GLFWwindow;

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

    /**
    @brief GLFW error sink, registered before glfwInit().
    */
    static void errorCallback(int error, const char* description);

    // Debugging
    bool m_showDebugOverlay = true;
    float m_fps = 0.0f;
    float m_frameTimeAccumulator = 0.0f;
    int m_frameCount = 0;
    float m_timeSinceLastPropagateMs = 1000.0f; // Start capped at >999 ms

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