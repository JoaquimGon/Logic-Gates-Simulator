#include "Input.h"

#include "Actions/EditorActions.h"
#include "ComponentShortcuts.h"
#include "Geometry/GridSystem.h"
#include "Scene.h"

#include <GLFW/glfw3.h>
#include <utility>

void Input::recordEdit(const EditResult& result)
{
    m_lastEditError = result.error;
    m_lastEditMessage = result.message;
}

bool Input::applyEdit(EditOperation operation)
{
    if (!m_scene)
        return false;
    const auto result = EditorActions(*m_scene).apply({std::move(operation)});
    recordEdit(result);
    return static_cast<bool>(result);
}

void Input::setScene(Scene* scene)
{
    if (m_scene == scene)
        return;
    cancelCurrentAction();
    m_pendingKeyPresses.clear();
    m_pressedKeys.clear();
    m_scene = scene;
    hoveredComponentId = hoveredPinComponentId = hoveredPinIndex = -1;
    hoveredWireId = INVALID_WIRE_ID;
    m_hoveredSegmentValid = false;
    m_lastEditError = EditError::None;
    m_lastEditMessage.clear();
}

void Input::setMode(EditorMode mode)
{
    if (m_mode == mode)
        return;
    cancelCurrentAction();
    m_pendingKeyPresses.clear();
    m_mode = mode;
}

void Input::mouseButtonCallback(GLFWwindow* window, int button, int action, int mods)
{
    if (auto* input = static_cast<Input*>(glfwGetWindowUserPointer(window)))
        input->handleMouseButton(window, button, action, mods);
}

void Input::cursorPositionCallback(GLFWwindow* window, double x, double y)
{
    if (auto* input = static_cast<Input*>(glfwGetWindowUserPointer(window)))
        input->handleCursorPos(window, x, y);
}

void Input::scrollCallback(GLFWwindow* window, double x, double y)
{
    if (auto* input = static_cast<Input*>(glfwGetWindowUserPointer(window)))
        input->handleScroll(window, x, y);
}

void Input::keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
    if (auto* input = static_cast<Input*>(glfwGetWindowUserPointer(window)))
        input->handleKey(key, action, mods);
}

void Input::focusCallback(GLFWwindow* window, int focused)
{
    if (!focused)
        if (auto* input = static_cast<Input*>(glfwGetWindowUserPointer(window)))
        {
            input->cancelCurrentAction();
            input->m_pendingKeyPresses.clear();
            input->m_pressedKeys.clear();
        }
}

void Input::handleKey(int key, int action, int mods)
{
    if (action == GLFW_RELEASE)
    {
        m_pressedKeys.erase(key);
        return;
    }
    if (action != GLFW_PRESS || !m_pressedKeys.insert(key).second)
        return;
    if (key == GLFW_KEY_F2)
    {
        if (!(mods & (GLFW_MOD_SHIFT | GLFW_MOD_CONTROL | GLFW_MOD_ALT | GLFW_MOD_SUPER)))
            setMode(
                m_mode == EditorMode::Selection ? EditorMode::Interaction : EditorMode::Selection
            );
        return;
    }
    m_pendingKeyPresses.insert(key);
}

bool Input::consumeKeyPress(int key)
{
    return m_pendingKeyPresses.erase(key) > 0;
}

void Input::cancelCurrentAction()
{
    if (m_scene)
        m_drag.cancel(*m_scene);
    m_wire.cancel();
    m_pan.cancel();
    m_selection.clear();
}

void Input::handleMouseButton(GLFWwindow* window, int button, int action, int mods)
{
    if (button == GLFW_MOUSE_BUTTON_RIGHT)
    {
        if (action == GLFW_PRESS)
        {
            if (m_drag.active() || m_wire.ownsPointer())
            {
                cancelCurrentAction();
                return;
            }
            m_selection.clear();
            glfwGetCursorPos(window, &lastMouseX, &lastMouseY);
            m_pan.begin(getLastMouse());
        }
        else if (action == GLFW_RELEASE)
            m_pan.cancel();
        return;
    }
    if (button != GLFW_MOUSE_BUTTON_LEFT || m_pan.active() || !m_scene)
        return;

    if (action == GLFW_PRESS)
    {
        if (!isIdle())
            return;
        updateHoverState(window);
        const auto hit = m_scene->hitTest(getMouseWorldCoord(window, m_zoom), mouseGridCoords);
        if (m_mode == EditorMode::Interaction)
        {
            if (hit.type == HitType::COMPONENT_BODY)
                m_scene->handleClick(hit.componentId);
            return;
        }
        if (hit.type == HitType::COMPONENT_BODY)
        {
            if (m_selection.selectComponent(hit.componentId))
                m_drag.begin(*m_scene, hit.componentId);
        }
        else if (hit.type == HitType::COMPONENT_PIN)
        {
            m_selection.selectPinOwner(hit.componentId);
            m_wire.begin(mouseGridCoords, {hit.componentId, hit.pinIndex}, hit.pinType);
        }
        else if (hit.wireId != INVALID_WIRE_ID)
        {
            m_selection.selectWire(*m_scene, hit.wireId, mouseGridCoords);
            m_wire.beginBranch(hit.wireId, mouseGridCoords);
        }
        else
        {
            m_selection.clear();
            m_wire.begin(mouseGridCoords);
        }
    }
    else if (action == GLFW_RELEASE)
    {
        if (m_drag.active())
            recordEdit(m_drag.finish(*m_scene));
        else if (m_wire.ownsPointer())
        {
            updateHoverState(window);
            const bool drawing = m_wire.active();
            const auto hit = m_scene->hitTest(getMouseWorldCoord(window, m_zoom), mouseGridCoords);
            if (auto command = m_wire.finish(hit))
                applyEdit(std::move(*command));
            if (drawing)
                m_selection.clear();
        }
        updateHoverState(window);
    }
}

void Input::handleCursorPos(GLFWwindow* window, double x, double y)
{
    mouseGridCoords = GridSystem::worldToGrid(getMouseWorldCoord(window, m_zoom));
    if (m_drag.active())
    {
        if (!m_scene || !m_drag.update(*m_scene, mouseGridCoords))
            cancelCurrentAction();
    }
    else if (m_wire.ownsPointer() && m_scene)
    {
        const bool wasDrawing = m_wire.active();
        m_wire.update(*m_scene, mouseGridCoords);
        if (!wasDrawing && m_wire.active())
            m_selection.clearSegment();
    }
    else if (m_pan.active())
    {
        int width, height;
        glfwGetWindowSize(window, &width, &height);
        m_pan.update(
            {static_cast<float>(x), static_cast<float>(y)},
            static_cast<float>(height),
            m_zoom,
            panOffset
        );
    }
    lastMouseX = x;
    lastMouseY = y;
}

void Input::process(GLFWwindow* window)
{
    if (consumeKeyPress(GLFW_KEY_ESCAPE))
        cancelCurrentAction();
    updateHoverState(window);
    const bool canEdit = m_scene && m_mode == EditorMode::Selection && isIdle();
    for (int key :
         {GLFW_KEY_1,
          GLFW_KEY_2,
          GLFW_KEY_3,
          GLFW_KEY_4,
          GLFW_KEY_5,
          GLFW_KEY_6,
          GLFW_KEY_7,
          GLFW_KEY_8,
          GLFW_KEY_9,
          GLFW_KEY_U,
          GLFW_KEY_I})
        if (consumeKeyPress(key) && canEdit)
            if (auto command = componentShortcut(
                    key, GridSystem::worldToGrid(getMouseWorldCoord(window, m_zoom))
                ))
                applyEdit(std::move(*command));

    const bool pause = consumeKeyPress(GLFW_KEY_SPACE);
    const bool step = consumeKeyPress(GLFW_KEY_PERIOD);
    if (m_scene && m_mode == EditorMode::Interaction)
    {
        if (pause)
            m_scene->togglePauseAllClocks();
        if (step)
            m_scene->stepAllClocks();
    }
    const bool remove = consumeKeyPress(GLFW_KEY_DELETE);
    const bool backspace = consumeKeyPress(GLFW_KEY_BACKSPACE);
    if (canEdit && (remove || backspace))
    {
        const auto hit = m_scene->hitTest(getMouseWorldCoord(window, m_zoom), mouseGridCoords);
        std::optional<SelectedSegment> hovered;
        if (const Wire* wire = m_scene->getWire(hit.wireId))
        {
            SelectedSegment segment;
            if (wire->getSegmentAt(mouseGridCoords, segment.start, segment.end))
                hovered = segment;
        }
        if (auto command = m_selection.deletion(hit, hovered))
            if (applyEdit(std::move(*command)))
                m_selection.clear();
    }
    updateHoverState(window);
}

void Input::handleScroll(GLFWwindow* window, double x, double y)
{
    if (glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS ||
        glfwGetKey(window, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS)
    {
        m_zoom += static_cast<float>(y) * 0.15f;
        if (m_zoom < 0.2f)
            m_zoom = 0.2f;
        if (m_zoom > 5.0f)
            m_zoom = 5.0f;
    }
}

void Input::updateHoverState(GLFWwindow* window)
{
    if (m_drag.active() || m_pan.active())
        return;
    if (!m_scene)
        return;

    glm::vec2 currentWorldCoords = getMouseWorldCoord(window, m_zoom);
    mouseGridCoords = GridSystem::worldToGrid(currentWorldCoords);

    hoveredComponentId = -1;
    hoveredPinIndex = -1;
    hoveredPinComponentId = -1;
    hoveredWireId = INVALID_WIRE_ID;
    m_hoveredSegmentValid = false;

    HitResult hit = m_scene->hitTest(currentWorldCoords, mouseGridCoords);
    switch (hit.type)
    {
    case HitType::COMPONENT_PIN:
        hoveredPinComponentId = hit.componentId;
        hoveredPinIndex = hit.pinIndex;
        hoveredPinType = hit.pinType;
        break;
    case HitType::WIRE_END:
        hoveredWireId = hit.wireId;
        break;
    case HitType::WIRE_START:
        hoveredWireId = hit.wireId;
        break;
    case HitType::WIRE_JUNCTION:
        hoveredWireId = hit.wireId;
        break;
    case HitType::WIRE_BODY:
        hoveredWireId = hit.wireId;
        break;
    case HitType::COMPONENT_BODY:
        hoveredComponentId = hit.componentId;
        break;
    default:
        break;
    }

    // Resolve the hovered id once and tolerate it being gone: the wire may have
    // been merged or split away since the hit test ran.
    const Wire* hoveredWire = m_scene->getWire(hoveredWireId);
    m_hoveredSegmentValid =
        hoveredWire &&
        hoveredWire->getSegmentAt(mouseGridCoords, m_hoveredSegmentStart, m_hoveredSegmentEnd);
}

glm::vec2 Input::getMouseWorldCoord(GLFWwindow* window, float zoom) const
{
    double mouseX, mouseY;
    glfwGetCursorPos(window, &mouseX, &mouseY);
    int width, height;
    glfwGetWindowSize(window, &width, &height);

    float ndcX = (2.0f * static_cast<float>(mouseX)) / width - 1.0f;
    float ndcY = 1.0f - (2.0f * static_cast<float>(mouseY)) / height;

    float aspectRatio =
        (height > 0) ? (static_cast<float>(width) / static_cast<float>(height)) : 1.0f;
    float correctedX = ndcX * aspectRatio;
    float correctedY = ndcY;

    float worldX = (correctedX / zoom) + panOffset.x;
    float worldY = (correctedY / zoom) + panOffset.y;

    return glm::vec2(worldX, worldY);
}