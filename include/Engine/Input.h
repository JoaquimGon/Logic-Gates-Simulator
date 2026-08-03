#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <iostream>

class Input
{
private:
    bool isDragging = false;
    double lastMouseX = 0.0;
    double lastMouseY = 0.0;
    glm::vec2 panOffset = glm::vec2(0.0f, 0.0f); // Use '=' or '{}' for member initialization

public:
    void process(GLFWwindow* window);
    void static mouseButtonCallback(GLFWwindow* window, int button, int action, int mods);
    void static cursorPositionCallback(GLFWwindow* window, double xpos, double ypos);

    void handleMouseButton(GLFWwindow* window, int button, int action, int mods);
    void handleCursorPos(GLFWwindow* window, double xpos, double ypos);

    glm::vec2 getPanOffset() const { return  panOffset; }
    glm::vec2 getLastMouse() const { return glm::vec2(lastMouseX, lastMouseY); }

};