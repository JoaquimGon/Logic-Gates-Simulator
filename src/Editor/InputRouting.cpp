#include "Editor/Input.h"

#include <GLFW/glfw3.h>
#include <cmath>
#include <stdexcept>
#include <utility>

void Input::setUiInputHandler(UiInputHandler handler)
{
    interruptCanvas();
    m_uiInputHandler = std::move(handler);
    m_uiCapture = {};
}

bool Input::dispatchUi(const UiInputEvent& event)
{
    // Keep the current callable alive if its owner replaces/unregisters it while handling an event.
    const auto handler = m_uiInputHandler;
    return handler && handler(event);
}

void Input::setUiCapture(UiInputCapture capture)
{
    const auto previous = m_uiCapture;
    m_uiCapture = capture;
    if (capture.keyboard && !previous.keyboard)
        setCanvasFocused(false);
    if ((capture.pointer && !previous.pointer) || (capture.keyboard && !previous.keyboard))
        interruptCanvas();
}

void Input::setCanvasFocused(bool focused)
{
    if (m_canvasFocused == focused)
        return;
    m_canvasFocused = focused;
    if (!focused)
        interruptCanvas();
}

void Input::setCanvasInputBounds(std::optional<CanvasInputBounds> bounds)
{
    if (bounds &&
        (!std::isfinite(bounds->x) || !std::isfinite(bounds->y) || !std::isfinite(bounds->width) ||
         !std::isfinite(bounds->height) || bounds->width < 0 || bounds->height < 0))
        throw std::invalid_argument(
            "Canvas input bounds must be finite with nonnegative dimensions."
        );
    if (m_canvasInputBounds == bounds)
        return;
    interruptCanvas();
    m_canvasInputBounds = bounds;
}

bool Input::canvasKeyboardAvailable() const
{
    return m_windowFocused && m_canvasFocused && !m_uiCapture.keyboard;
}

bool Input::containsCanvasPoint(GLFWwindow* window, double x, double y) const
{
    int width, height;
    glfwGetWindowSize(window, &width, &height);
    if (width <= 0 || height <= 0)
        return false;
    const CanvasInputBounds fullWindow{
        0, 0, static_cast<double>(width), static_cast<double>(height)
    };
    return fullWindow.contains(x, y) &&
           (!m_canvasInputBounds || m_canvasInputBounds->contains(x, y));
}

bool Input::isCanvasPointerAvailable(GLFWwindow* window) const
{
    double x, y;
    glfwGetCursorPos(window, &x, &y);
    return m_windowFocused && !m_uiCapture.pointer && containsCanvasPoint(window, x, y);
}

void Input::clearHover()
{
    hoveredComponentId = hoveredPinComponentId = hoveredPinIndex = -1;
    hoveredWireId = INVALID_WIRE_ID;
    m_hoveredSegmentValid = false;
}

void Input::cancelGestures()
{
    if (m_scene)
        m_drag.cancel(*m_scene);
    m_wire.cancel();
    m_pan.cancel();
    m_canvasMouseButtons.clear();
}

void Input::interruptCanvas()
{
    cancelGestures();
    clearHover();
    m_pendingKeyPresses.clear();
}

void Input::charCallback(GLFWwindow* window, unsigned int codepoint)
{
    if (auto* input = static_cast<Input*>(glfwGetWindowUserPointer(window)))
        input->handleText(codepoint);
}

void Input::handleText(std::uint32_t codepoint)
{
    UiInputEvent event{UiInputKind::Text};
    event.codepoint = codepoint;
    if (dispatchUi(event))
    {
        setCanvasFocused(false);
        interruptCanvas();
    }
}

void Input::handleFocus(bool focused)
{
    UiInputEvent event{UiInputKind::WindowFocus};
    event.focused = focused;
    dispatchUi(event);
    m_windowFocused = focused;
    if (!focused)
    {
        cancelCurrentAction();
        clearHover();
        m_pendingKeyPresses.clear();
        m_pressedKeys.clear();
        m_pressedMouseButtons.clear();
    }
}
