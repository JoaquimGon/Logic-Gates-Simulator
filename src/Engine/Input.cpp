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
            glm::vec2 mouseWorldCoord = getMouseWorldCoord(window, m_zoom);
            std::cout << "Wolrd coordinate (" << mouseWorldCoord.x << ", " << mouseWorldCoord.y << ").\n";
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

glm::vec2 Input::getMouseWorldCoord(GLFWwindow* window, float zoom)
{
    // 1. Get the current cursor position from GLFW
    double mouseX, mouseY;
    glfwGetCursorPos(window, &mouseX, &mouseY);

    // 2. Get window dimensions
    int width, height;
    glfwGetWindowSize(window, &width, &height);

    // 3. Convert screen pixels to Normalized Device Coordinates (-1 to 1)
    float ndcX = (2.0f * static_cast<float>(mouseX)) / width - 1.0f;
    float ndcY = 1.0f - (2.0f * static_cast<float>(mouseY)) / height; // Invert Y because screen Y goes down

    // 4. Apply the same aspect ratio correction you use in your grid shader
    float aspectRatio = static_cast<float>(width) / static_cast<float>(height);
    float correctedX = ndcX * aspectRatio;
    float correctedY = ndcY;

    // 5. Reverse the shader math: divide by zoom, then add panOffset
    float worldX = (correctedX / zoom) + panOffset.x;
    float worldY = (correctedY / zoom) + panOffset.y;

    return glm::vec2(worldX, worldY);
}