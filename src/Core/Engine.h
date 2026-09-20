#pragma once

#include "..\Logic\Circuit.h"
#include "Input.h"
#include "..\Logic\Wire.h"
#include "..\Views\GridSystem.h"
#include "..\Graphics\Renderer.h"
#include "..\Views\ComponentView.h"
#include "..\Views\GateView.h"
#include "..\Views\InputPinView.h"
#include "Scene.h"

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
    GLFWwindow* window = nullptr; // starts null so shutdown() is safe before init()
    std::string m_windowName;
    int m_windowWidth = 0;
    int m_windowHeight = 0;
    bool m_glfwInitialized = false;

    Input input;

    // NEW: The Renderer now owns all meshes, shaders, and OpenGL state
    Renderer m_renderer;

    static void resizeWindow(GLFWwindow* window, int width, int height)
    {
        glViewport(0, 0, width, height);
    }

public:
    Engine(std::string windowName, int windowWidth, int windowHeight);
    ~Engine();

    int init();
    void run();

    /**
    * @brief Releases the renderer's GL objects, the window and GLFW, in that order.
    * GL objects can only be deleted while their context is still current, so this
    * must run before glfwTerminate(). It is idempotent, which also makes it usable
    * from the failure paths of init() and from the destructor.
    */
    void shutdown();

};