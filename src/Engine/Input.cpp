#include "..\..\include\Engine\Input.h"


// 1. Static bridge for mouse buttons
void Input::mouseButtonCallback(GLFWwindow* window, int button, int action, int mods)
{
    std::cout << "Mouse call back.\n";
    // Retrieve the pointer to the active Input instance stored in the window
    Input* handler = static_cast<Input*>(glfwGetWindowUserPointer(window));
    if (handler) {
        handler->handleMouseButton(window, button, action, mods);
    }
}

// 2. Static bridge for cursor position
void Input::cursorPositionCallback(GLFWwindow* window, double xpos, double ypos)
{
    Input* handler = static_cast<Input*>(glfwGetWindowUserPointer(window));
    if (handler) {
        handler->handleCursorPos(window, xpos, ypos);
    }
}

// 3. Your real, non-static mouse button logic (can use 'isDragging', 'panOffset', etc.)
void Input::handleMouseButton(GLFWwindow* window, int button, int action, int mods)
{

    if (button == GLFW_MOUSE_BUTTON_LEFT) {
        std::cout << "Mout left button clicked.\n";
        if (action == GLFW_PRESS) {
            isDragging = true;
            glfwGetCursorPos(window, &lastMouseX, &lastMouseY);
        }
        else if (action == GLFW_RELEASE) {
            isDragging = false;
        }
    }
}

// 4. Your real, non-static cursor position logic
void Input::handleCursorPos(GLFWwindow* window, double xpos, double ypos)
{
    if (isDragging) {
        double deltaX = xpos - lastMouseX;
        double deltaY = ypos - lastMouseY;

        int width, height;
        glfwGetWindowSize(window, &width, &height);

        panOffset.x -= (static_cast<float>(deltaX) / height) * 2.0f;
        panOffset.y += (static_cast<float>(deltaY) / height) * 2.0f;

        lastMouseX = xpos;
        lastMouseY = ypos;
    }
}

void Input::process(GLFWwindow* window)
{
    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
        glfwSetWindowShouldClose(window, true);
}