#include "Input.h"

#include "Actions/EditorActions.h"
#include "Geometry/GridSystem.h"
#include "Scene.h"

#include <GLFW/glfw3.h>
#include <cmath>
#include <utility>

bool Input::applyEdit(EditOperation operation)
{
    if (!m_scene)
        return false;
    const auto result = EditorActions(*m_scene).apply({std::move(operation)});
    m_lastEditError = result.error;
    m_lastEditMessage = result.message;
    return static_cast<bool>(result);
}

void Input::setScene(Scene* scene)
{
    if (m_scene == scene)
        return;
    cancelCurrentAction();
    m_pendingKeyPresses.clear();
    m_scene = scene;
    hoveredComponentId = hoveredPinComponentId = hoveredPinIndex = -1;
    hoveredWireId = INVALID_WIRE_ID;
    m_hoveredSegmentValid = false;
    m_wireOriginComponentId = -1;
    m_wireOriginPin = {};
    m_lastEditError = EditError::None;
    m_lastEditMessage.clear();
}

void Input::mouseButtonCallback(GLFWwindow* window, int button, int action, int mods)
{
    Input* handler = static_cast<Input*>(glfwGetWindowUserPointer(window));
    if (handler)
        handler->handleMouseButton(window, button, action, mods);
}


void Input::cursorPositionCallback(GLFWwindow* window, double xpos, double ypos)
{
    Input* handler = static_cast<Input*>(glfwGetWindowUserPointer(window));
    if (handler)
        handler->handleCursorPos(window, xpos, ypos);
}


void Input::scrollCallback(GLFWwindow* window, double xoffset, double yoffset)
{
    Input* input = static_cast<Input*>(glfwGetWindowUserPointer(window));
    if (input)
        input->handleScroll(window, xoffset, yoffset);
}


void Input::keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
    Input* input = static_cast<Input*>(glfwGetWindowUserPointer(window));
    if (input)
        input->handleKey(key, action);
}


void Input::handleKey(int key, int action)
{
    if (action == GLFW_PRESS)
        m_pendingKeyPresses.insert(key);
}


bool Input::consumeKeyPress(int key)
{
    return m_pendingKeyPresses.erase(key) > 0;
}


void Input::cancelCurrentAction()
{
    if (m_movePreview && m_scene)
        EditorActions(*m_scene).cancelMove(*m_movePreview);
    m_movePreview.reset();

    if (m_state == InteractionState::DRAWING_WIRE)
    {
        activeWire = Wire();
        m_wireOriginComponentId = -1;
        baseWirePath.clear();
        isMidWireBranchPending = false;
    }

    m_selectedComponentId = -1;
    m_selectedWireId = INVALID_WIRE_ID;
    m_hasSelectedSegment = false;
    m_state = InteractionState::IDLE;
}


void Input::handleMouseButton(GLFWwindow* window, int button, int action, int mods)
{
    if (button == GLFW_MOUSE_BUTTON_RIGHT)
    {
        if (action == GLFW_PRESS)
        {
            if (m_state == InteractionState::DRAWING_WIRE ||
                m_state == InteractionState::DRAGGING_GATE)
            {
                cancelCurrentAction();
                return;
            }
            m_selectedComponentId = -1;
            m_selectedWireId = INVALID_WIRE_ID;
            m_hasSelectedSegment = false;
            isMidWireBranchPending = false;
            m_wireOriginComponentId = -1;

            glfwGetCursorPos(window, &lastMouseX, &lastMouseY);
            m_state = InteractionState::PANNING;
        }
        else if (action == GLFW_RELEASE && m_state == InteractionState::PANNING)
        {
            m_state = InteractionState::IDLE;
        }
    }

    // State guard panning so it doesn't get overwritten
    if (m_state == InteractionState::PANNING)
        return;

    if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS &&
        m_state == InteractionState::DRAGGING_GATE)
        return;

    if (button == GLFW_MOUSE_BUTTON_LEFT && m_scene)
    {
        if (action == GLFW_PRESS)
        {
            glfwGetCursorPos(window, &lastMouseX, &lastMouseY);
            updateHoverState(window);

            const int clickedComponentId =
                hoveredPinComponentId != -1 ? hoveredPinComponentId : hoveredComponentId;

            // Clicking an already-selected gate deselects without creating an edit.
            const bool clickedSelectedGate =
                m_selectedComponentId != -1 && clickedComponentId == m_selectedComponentId &&
                dynamic_cast<InputPin*>(m_scene->getLogicComponent(clickedComponentId)) == nullptr;

            if (clickedSelectedGate)
            {
                cancelCurrentAction();
                return;
            }

            m_selectedComponentId = -1;
            m_selectedWireId = INVALID_WIRE_ID;
            m_hasSelectedSegment = false;
            isMidWireBranchPending = false;
            m_wireOriginComponentId = -1;

            if (hoveredPinComponentId != -1 && hoveredPinIndex != -1)
            {
                m_selectedComponentId = hoveredPinComponentId;
                m_wireOriginComponentId = hoveredPinComponentId;

                activeWire = Wire();
                m_wireOriginPin = {hoveredPinComponentId, hoveredPinIndex};
                m_wireOriginType = hoveredPinType;

                activeWire.setState(PinState::DISCONNECTED);
                baseWirePath = {mouseGridCoords};
                wireStartPos = mouseGridCoords;
                wireAxisLocked = false;
                wireAxisXFirst = true;
                m_state = InteractionState::DRAWING_WIRE;
            }
            else if (hoveredWireId != INVALID_WIRE_ID)
            {
                m_selectedWireId = hoveredWireId;
                const Wire* clickedWire = m_scene->getWire(hoveredWireId);
                m_hasSelectedSegment =
                    clickedWire && clickedWire->getSegmentAt(
                                       mouseGridCoords, m_selectedSegmentStart, m_selectedSegmentEnd
                                   );

                wireStartPos = mouseGridCoords;
                isMidWireBranchPending = true;
                wireAxisLocked = false;
                wireAxisXFirst = true;

                // Set the drawing wire to disconnected
                activeWire = Wire();
                activeWire.setState(PinState::DISCONNECTED);
                m_wireOriginPin = PinRef();
                m_wireOriginType = PinType::INPUT;
            }
            else if (hoveredComponentId != -1)
            {
                m_selectedComponentId = hoveredComponentId;

                if (!m_scene->handleClick(hoveredComponentId))
                {
                    m_movePreview = EditorActions(*m_scene).beginMove(hoveredComponentId);
                    if (m_movePreview)
                        m_state = InteractionState::DRAGGING_GATE;
                }
            }
            else
            {
                activeWire = Wire();
                baseWirePath = {mouseGridCoords};
                wireStartPos = mouseGridCoords;
                wireAxisLocked = false;
                wireAxisXFirst = true;
                m_state = InteractionState::DRAWING_WIRE;
            }
        }
        else if (action == GLFW_RELEASE)
        {
            if (isMidWireBranchPending)
            {
                isMidWireBranchPending = false;
                m_state = InteractionState::IDLE;
                return;
            }

            if (m_state == InteractionState::DRAGGING_GATE && m_movePreview)
            {
                const auto result = EditorActions(*m_scene).commitMove(*m_movePreview);
                m_lastEditError = result.error;
                m_lastEditMessage = result.message;
                m_movePreview.reset();
                m_state = InteractionState::IDLE;
            }
            else if (m_state == InteractionState::DRAWING_WIRE)
            {
                if (activeWire.getPath().size() <= 1)
                {
                    m_state = InteractionState::IDLE;
                    activeWire = Wire();
                    m_selectedComponentId = -1;
                    m_wireOriginComponentId = -1;
                    return;
                }

                updateHoverState(window);

                // Disallow self-connecting wires directly to the origin
                // component
                if (hoveredPinComponentId != -1 && m_wireOriginComponentId != -1 &&
                    hoveredPinComponentId == m_wireOriginComponentId)
                {
                    cancelCurrentAction();
                    return;
                }

                // Geometry is committed directly; Scene::settleGeometry()
                // splits overlapping and intersecting geometry cleanly in one
                // place.
                if (activeWire.getPath().size() >= 2)
                {
                    applyEdit(AddWire{activeWire.getPath()});
                }

                cancelCurrentAction();
            }
        }
    }
}


void Input::handleCursorPos(GLFWwindow* window, double xpos, double ypos)
{
    glm::vec2 worldCoords = getMouseWorldCoord(window, m_zoom);
    GridCoords snappedGridPos = GridSystem::worldToGrid(worldCoords);

    if (isMidWireBranchPending && snappedGridPos != wireStartPos && m_scene)
    {
        if (const Wire* selectedWire = m_scene->getWire(m_selectedWireId))
        {
            const Wire& target = *selectedWire;

            m_wireOriginPin = PinRef();
            m_wireOriginType = PinType::INPUT;
            m_wireOriginComponentId = -1;
            if (const Net* net = m_scene->getNet(target.getNet()))
            {
                if (net->hasDriver())
                {
                    m_wireOriginPin = *net->getDriver();
                    m_wireOriginType = PinType::OUTPUT;
                    m_wireOriginComponentId = m_wireOriginPin.componentId;
                }
                else if (!net->getSinks().empty())
                {
                    m_wireOriginComponentId = net->getSinks().front().componentId;
                }
            }

            activeWire = Wire();
            if (const Net* net = m_scene->getNet(target.getNet()))
            {
                activeWire.setState(
                    net->getState() == PinState::ON ? PinState::ON : PinState::DISCONNECTED
                );
            }
            else
            {
                activeWire.setState(PinState::DISCONNECTED);
            }

            baseWirePath = {wireStartPos};
            m_state = InteractionState::DRAWING_WIRE;
            m_hasSelectedSegment = false;
        }
        isMidWireBranchPending = false;
    }

    if (m_state == InteractionState::DRAGGING_GATE && m_movePreview)
    {
        if (!m_scene || !EditorActions(*m_scene).previewMove(*m_movePreview, snappedGridPos))
            cancelCurrentAction();
        lastMouseX = xpos;
        lastMouseY = ypos;
        return;
    }

    if (m_state == InteractionState::DRAWING_WIRE)
    {
        if (snappedGridPos != wireStartPos)
        {
            int dx = snappedGridPos.x - wireStartPos.x;
            int dy = snappedGridPos.y - wireStartPos.y;
            if (!wireAxisLocked)
            {
                wireAxisXFirst = (std::abs(dx) >= std::abs(dy));
                wireAxisLocked = true;
            }
        }
        else
        {
            wireAxisLocked = false;
        }

        std::vector<GridCoords> previewPath = baseWirePath;
        if (snappedGridPos != wireStartPos)
        {
            if (wireStartPos.x != snappedGridPos.x && wireStartPos.y != snappedGridPos.y)
            {
                if (wireAxisXFirst)
                    previewPath.push_back({snappedGridPos.x, wireStartPos.y});
                else
                    previewPath.push_back({wireStartPos.x, snappedGridPos.y});
            }
            previewPath.push_back(snappedGridPos);
        }
        activeWire.setPath(previewPath);
    }

    if (m_state == InteractionState::PANNING)
    {
        double deltaX = xpos - lastMouseX;
        double deltaY = ypos - lastMouseY;
        int width, height;
        glfwGetWindowSize(window, &width, &height);

        panOffset.x -= ((static_cast<float>(deltaX) / height) * 2.0f) / m_zoom;
        panOffset.y += ((static_cast<float>(deltaY) / height) * 2.0f) / m_zoom;
    }

    lastMouseX = xpos;
    lastMouseY = ypos;
}


void Input::process(GLFWwindow* window)
{
    // Every press is recorded by keyCallback() and drained here, so a key that
    // is tapped between two frames still acts, and Escape no longer has to be
    // held down.
    if (consumeKeyPress(GLFW_KEY_ESCAPE))
        cancelCurrentAction();

    // Spawning and deleting only apply while no gesture owns the mouse. The
    // press is consumed regardless of this flag, so a key pressed during a drag
    // cannot fire later, out of context.
    const bool canSpawn = (m_scene != nullptr) && isIdle();

    auto trySpawnGate = [&](GateType type, const std::string& shaderName)
    {
        if (!canSpawn)
            return;

        bool isInverted =
            (type == GateType::NAND || type == GateType::NOR || type == GateType::NXOR);

        std::vector<PinUI> inPins = {
            {PinType::INPUT, 0, PinState::DISCONNECTED, {-2, 1}},
            {PinType::INPUT, 1, PinState::DISCONNECTED, {-2, -1}}
        };

        // Push the output pin out 1 grid cell for inverted gates to sit on the
        // bubble
        std::vector<PinUI> outPins = {
            {PinType::OUTPUT, 0, PinState::DISCONNECTED, {isInverted ? 3 : 2, 0}}
        };

        // Widen the bounding box to 0.3 for inverted gates
        glm::vec2 size = isInverted ? glm::vec2{0.3f, 0.2f} : glm::vec2{0.2f, 0.2f};

        glm::vec2 worldPos = getMouseWorldCoord(window, m_zoom);
        GridCoords gridPos = GridSystem::worldToGrid(worldPos);

        applyEdit(
            CreateGate{
                type, gridPos, {size, shaderName, inPins, outPins}, PlacementPolicy::FindFree
            }
        );
    };

    // 1. SPAWN INPUT PIN (Key '1')
    if (consumeKeyPress(GLFW_KEY_1) && canSpawn)
    {
        glm::vec2 worldPos = getMouseWorldCoord(window, m_zoom);
        GridCoords gridPos = GridSystem::worldToGrid(worldPos);

        applyEdit(
            CreateInput{gridPos, {0.15f, 0.15f}, "inputPin", false, PlacementPolicy::FindFree}
        );
    }

    // 2. SPAWN NOT GATE (Key '2')
    if (consumeKeyPress(GLFW_KEY_2) && canSpawn)
    {
        // Pins spaced exactly 3 grid cells apart
        std::vector<PinUI> inPins{{PinType::INPUT, 0, PinState::DISCONNECTED, {-2, 0}}};
        std::vector<PinUI> outPins{{PinType::OUTPUT, 0, PinState::DISCONNECTED, {1, 0}}};

        glm::vec2 worldPos = getMouseWorldCoord(window, m_zoom);
        GridCoords gridPos = GridSystem::worldToGrid(worldPos);

        // Bounding box must be 4 cells wide (0.2f) so edges land on integer
        // grid lines
        applyEdit(
            CreateGate{
                NOT, gridPos, {{0.2f, 0.1f}, "NOTgate", inPins, outPins}, PlacementPolicy::FindFree
            }
        );
    }

    // 3. SPAWN AND GATE (Key '3')
    if (consumeKeyPress(GLFW_KEY_3))
        trySpawnGate(GateType::AND, "ANDgate");

    // 4. SPAWN NAND GATE (Key '4')
    if (consumeKeyPress(GLFW_KEY_4))
        trySpawnGate(GateType::NAND, "NANDgate");

    // 5. SPAWN OR GATE (Key '5')
    if (consumeKeyPress(GLFW_KEY_5))
        trySpawnGate(GateType::OR, "ORgate");

    // 6. SPAWN NOR GATE (Key '6')
    if (consumeKeyPress(GLFW_KEY_6))
        trySpawnGate(GateType::NOR, "NORgate");

    // 7. SPAWN XOR GATE (Key '7')
    if (consumeKeyPress(GLFW_KEY_7))
        trySpawnGate(GateType::XOR, "XORgate");

    // 8. SPAWN NXOR GATE (Key '8')
    if (consumeKeyPress(GLFW_KEY_8))
        trySpawnGate(GateType::NXOR, "NXORgate");

    // 9. SPAWN CLOCK (Key '9')
    if (consumeKeyPress(GLFW_KEY_9) && canSpawn)
    {
        glm::vec2 worldPos = getMouseWorldCoord(window, m_zoom);
        GridCoords gridPos = GridSystem::worldToGrid(worldPos);

        applyEdit(CreateClock{gridPos, {0.15f, 0.15f}, "clock", 1.0f, PlacementPolicy::FindFree});
    }

    if (consumeKeyPress(GLFW_KEY_U) && canSpawn)
    {
        glm::vec2 worldPos = getMouseWorldCoord(window, m_zoom);
        GridCoords gridPos = GridSystem::worldToGrid(worldPos);
        applyEdit(CreateLatch{LatchType::SR_LATCH, gridPos, PlacementPolicy::FindFree});
    }

    // Spawn Gated D Latch (Key 'I')
    if (consumeKeyPress(GLFW_KEY_I) && canSpawn)
    {
        glm::vec2 worldPos = getMouseWorldCoord(window, m_zoom);
        GridCoords gridPos = GridSystem::worldToGrid(worldPos);
        applyEdit(CreateLatch{LatchType::D_LATCH, gridPos, PlacementPolicy::FindFree});
    }

    // Toggle clock(s) pause on Space
    if (consumeKeyPress(GLFW_KEY_SPACE) && m_scene)
    {
        m_scene->togglePauseAllClocks();
    }

    // Single-step on Period key
    if (consumeKeyPress(GLFW_KEY_PERIOD) && m_scene)
    {
        m_scene->stepAllClocks();
    }

    // Adjust frequency: Up Arrow = faster (+1 Hz), Down Arrow = slower (-1 Hz)
    if (consumeKeyPress(GLFW_KEY_UP) && m_scene)
    {
        // scale up or down
    }

    // ==========================================
    // DELETE SELECTED OR HOVERED (Delete or Backspace)
    // ==========================================
    // Both delete keys are drained before the action is attempted, so a press
    // of either one cannot stay queued into the next frame.
    const bool deletePressed = consumeKeyPress(GLFW_KEY_DELETE);
    const bool backspacePressed = consumeKeyPress(GLFW_KEY_BACKSPACE);

    if (deletePressed || backspacePressed)
    {
        if (canSpawn)
        {
            int compToDelete =
                m_selectedComponentId != -1 ? m_selectedComponentId : hoveredComponentId;
            WireId wireToDelete =
                m_selectedWireId != INVALID_WIRE_ID ? m_selectedWireId : hoveredWireId;

            if (compToDelete != -1)
            {
                if (applyEdit(DeleteComponent{compToDelete}) &&
                    m_selectedComponentId == compToDelete)
                    m_selectedComponentId = -1;
            }
            else if (m_scene->getWire(wireToDelete))
            {
                bool removed;
                if (m_selectedWireId == wireToDelete && m_hasSelectedSegment)
                    removed = applyEdit(
                        DeleteWireSegment{
                            wireToDelete, m_selectedSegmentStart, m_selectedSegmentEnd
                        }
                    );
                else if (wireToDelete == hoveredWireId && m_hoveredSegmentValid)
                    removed = applyEdit(
                        DeleteWireSegment{wireToDelete, m_hoveredSegmentStart, m_hoveredSegmentEnd}
                    );
                else
                    removed = applyEdit(DeleteWire{wireToDelete});

                if (removed && m_selectedWireId == wireToDelete)
                {
                    m_selectedWireId = INVALID_WIRE_ID;
                    m_hasSelectedSegment = false;
                }
            }

            updateHoverState(window);
        }
    }

    updateHoverState(window);
}


void Input::handleScroll(GLFWwindow* window, double xoffset, double yoffset)
{
    bool ctrlPressed = (glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS) ||
                       (glfwGetKey(window, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS);
    if (ctrlPressed)
    {
        float zoomSpeed = 0.15f;
        m_zoom += static_cast<float>(yoffset) * zoomSpeed;
        if (m_zoom < 0.2f)
            m_zoom = 0.2f;
        if (m_zoom > 5.0f)
            m_zoom = 5.0f;
    }
}


void Input::updateHoverState(GLFWwindow* window)
{
    if (m_state == InteractionState::DRAGGING_GATE || m_state == InteractionState::PANNING)
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