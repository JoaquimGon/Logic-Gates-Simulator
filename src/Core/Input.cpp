#include "Input.h"
#include <algorithm>
#include <cmath>

void Input::mouseButtonCallback(GLFWwindow* window, int button, int action, int mods) {
    Input* handler = static_cast<Input*>(glfwGetWindowUserPointer(window));
    if (handler) handler->handleMouseButton(window, button, action, mods);
}

void Input::cursorPositionCallback(GLFWwindow* window, double xpos, double ypos) {
    Input* handler = static_cast<Input*>(glfwGetWindowUserPointer(window));
    if (handler) handler->handleCursorPos(window, xpos, ypos);
}

void Input::scrollCallback(GLFWwindow* window, double xoffset, double yoffset) {
    Input* input = static_cast<Input*>(glfwGetWindowUserPointer(window));
    if (input) input->handleScroll(window, xoffset, yoffset);
}

void Input::cancelCurrentAction() {
    if (m_state == InteractionState::DRAWING_WIRE) {
        if (m_scene && !isMidWireBranchPending &&
            (activeWire.hasSource() || activeWire.hasDest() || activeWire.getPath().size() > 1)) {

            if (activeWire.hasSource() && activeWire.hasDest()) {
                m_scene->connectPins(activeWire.getSource().componentId, activeWire.getDest().componentId, activeWire.getDest().pinIndex);
            }
            m_scene->commitWire(activeWire);
        }
        activeWire = Wire();
        baseWirePath.clear();
        isMidWireBranchPending = false;
    }

    m_draggedComponent = nullptr;
    m_selectedComponentId = -1;
    m_selectedWireIndex = -1;
    m_hasSelectedSegment = false;
    m_state = InteractionState::IDLE;
}

void Input::handleMouseButton(GLFWwindow* window, int button, int action, int mods)
{
    if (button == GLFW_MOUSE_BUTTON_RIGHT) {
        if (action == GLFW_PRESS) {
            if (m_state == InteractionState::DRAWING_WIRE || m_state == InteractionState::DRAGGING_GATE) {
                cancelCurrentAction();
                return;
            }
            m_selectedComponentId = -1;
            m_selectedWireIndex = -1;
            m_hasSelectedSegment = false;
            isMidWireBranchPending = false;

            glfwGetCursorPos(window, &lastMouseX, &lastMouseY);
            m_state = InteractionState::PANNING;
        }
        else if (action == GLFW_RELEASE && m_state == InteractionState::PANNING) {
            m_state = InteractionState::IDLE;
        }
    }

    if (button == GLFW_MOUSE_BUTTON_LEFT && m_scene) {
        if (action == GLFW_PRESS) {
            glfwGetCursorPos(window, &lastMouseX, &lastMouseY);
            updateHoverState(window);

            m_selectedComponentId = -1;
            m_selectedWireIndex = -1;
            m_hasSelectedSegment = false;
            isMidWireBranchPending = false;

            if (hoveredComponentId != -1 && hoveredPinIndex != -1) {
                m_selectedComponentId = hoveredComponentId;

                activeWire = Wire();
                if (hoveredPinType == PinType::INPUT) activeWire.setDest(hoveredComponentId, hoveredPinIndex);
                else activeWire.setSource(hoveredComponentId, hoveredPinIndex);

                activeWire.setState(PinState::DISCONNECTED);
                baseWirePath = { mouseGridCoords };
                wireStartPos = mouseGridCoords;
                wireAxisLocked = false;
                wireAxisXFirst = true;
                m_state = InteractionState::DRAWING_WIRE;
            }
            else if (hoveredWireIndex != -1 && (isHoveredWireStart || isHoveredWireEnd)) {
                m_selectedWireIndex = hoveredWireIndex;

                Wire picked = m_scene->extractWire(static_cast<size_t>(hoveredWireIndex));
                m_hasSelectedSegment = picked.getSegmentAt(mouseGridCoords, m_selectedSegmentStart, m_selectedSegmentEnd);

                activeWire = picked;
                baseWirePath = picked.getPath();

                if (picked.hasSource() && picked.hasDest()) {
                    m_scene->disconnectPins(picked.getSource().componentId, picked.getDest().componentId, picked.getDest().pinIndex);
                }

                if (isHoveredWireStart) {
                    std::reverse(baseWirePath.begin(), baseWirePath.end());
                    activeWire.disconnectSource();
                }
                else {
                    activeWire.disconnectDest();
                }

                wireStartPos = mouseGridCoords;
                wireAxisLocked = false;
                wireAxisXFirst = true;
                hoveredWireIndex = -1;
                m_state = InteractionState::DRAWING_WIRE;
            }
            else if (hoveredWireIndex != -1) {
                m_selectedWireIndex = hoveredWireIndex;
                m_hasSelectedSegment = m_scene->wireAt(static_cast<size_t>(hoveredWireIndex))
                    .getSegmentAt(mouseGridCoords, m_selectedSegmentStart, m_selectedSegmentEnd);

                wireStartPos = mouseGridCoords;
                isMidWireBranchPending = true;
                wireAxisLocked = false;
                wireAxisXFirst = true;
            }
            else if (hoveredComponentId != -1) {
                m_selectedComponentId = hoveredComponentId;

                if (!m_scene->handleClick(hoveredComponentId)) {
                    m_draggedComponent = m_scene->getComponentView(hoveredComponentId);
                    m_state = InteractionState::DRAGGING_GATE;
                }
                // else: the component handled the click itself (e.g. InputPinView toggled) —
                // stay IDLE, don't start a drag.
            }
            else {
                activeWire = Wire();
                baseWirePath = { mouseGridCoords };
                wireStartPos = mouseGridCoords;
                wireAxisLocked = false;
                wireAxisXFirst = true;
                m_state = InteractionState::DRAWING_WIRE;
            }
        }
        else if (action == GLFW_RELEASE) {
            if (isMidWireBranchPending) {
                isMidWireBranchPending = false;
                m_state = InteractionState::IDLE;
                return;
            }

            if (m_state == InteractionState::DRAGGING_GATE && m_draggedComponent) {
                m_scene->reconnectWiresToComponent(m_draggedComponent->getComponentId());
                m_draggedComponent = nullptr;
                m_state = InteractionState::IDLE;
            }
            else if (m_state == InteractionState::DRAWING_WIRE) {
                if (mouseGridCoords == wireStartPos && activeWire.getPath().size() <= 1) {
                    if (activeWire.hasSource() || activeWire.hasDest() || activeWire.getPath().size() > 0) {
                        m_selectedWireIndex = static_cast<int>(m_scene->commitWire(activeWire));
                    }
                    m_state = InteractionState::IDLE;
                    return;
                }

                updateHoverState(window);

                if (hoveredComponentId != -1 && hoveredPinIndex != -1) {
                    bool isSamePin = (hoveredPinType == PinType::INPUT && activeWire.hasDest() &&
                        activeWire.getDest().componentId == hoveredComponentId && activeWire.getDest().pinIndex == hoveredPinIndex) ||
                        (hoveredPinType == PinType::OUTPUT && activeWire.hasSource() &&
                            activeWire.getSource().componentId == hoveredComponentId && activeWire.getSource().pinIndex == hoveredPinIndex);

                    if (isSamePin) {
                        if (hoveredPinType == PinType::INPUT) activeWire.disconnectDest();
                        else activeWire.disconnectSource();
                    }
                    else {
                        if (hoveredPinType == PinType::INPUT && !activeWire.hasDest()) activeWire.setDest(hoveredComponentId, hoveredPinIndex);
                        else if (hoveredPinType == PinType::OUTPUT && !activeWire.hasSource()) activeWire.setSource(hoveredComponentId, hoveredPinIndex);

                        if (activeWire.hasSource() && activeWire.hasDest()) {
                            m_scene->connectPins(activeWire.getSource().componentId, activeWire.getDest().componentId, activeWire.getDest().pinIndex);
                        }
                    }
                }
                else if (hoveredWireIndex != -1) {
                    Wire wireA, wireB;
                    if (m_scene->splitWireAt(static_cast<size_t>(hoveredWireIndex), mouseGridCoords, wireA, wireB)) {
                        bool activeHasSrc = activeWire.hasSource();
                        bool activeHasDst = activeWire.hasDest();
                        bool targetHasSrc = wireA.hasSource() || wireB.hasSource();
                        bool targetHasDst = wireA.hasDest() || wireB.hasDest();

                        WireEndpoint src = activeHasSrc ? activeWire.getSource() : (wireA.hasSource() ? wireA.getSource() : wireB.getSource());
                        WireEndpoint dst = activeHasDst ? activeWire.getDest() : (wireA.hasDest() ? wireA.getDest() : wireB.getDest());

                        if (activeHasSrc || targetHasSrc) {
                            activeWire.setSource(src.componentId, src.pinIndex);
                            wireA.setSource(src.componentId, src.pinIndex);
                            wireB.setSource(src.componentId, src.pinIndex);
                        }
                        if (activeHasDst || targetHasDst) {
                            activeWire.setDest(dst.componentId, dst.pinIndex);
                            wireA.setDest(dst.componentId, dst.pinIndex);
                            wireB.setDest(dst.componentId, dst.pinIndex);
                        }

                        if ((activeHasSrc && targetHasDst) || (activeHasDst && targetHasSrc)) {
                            m_scene->connectPins(src.componentId, dst.componentId, dst.pinIndex);
                        }

                        m_scene->addWires(wireA, wireB);
                    }
                }

                if (activeWire.getPath().size() > 1) {
                    activeWire.simplifyPath();
                    m_scene->commitWire(activeWire);
                }

                m_selectedWireIndex = -1;
                m_hasSelectedSegment = false;
                m_state = InteractionState::IDLE;
            }
        }
    }
}

void Input::handleCursorPos(GLFWwindow* window, double xpos, double ypos)
{
    glm::vec2 worldCoords = getMouseWorldCoord(window, m_zoom);
    GridCoords snappedGridPos = GridSystem::worldToGrid(worldCoords);

    if (isMidWireBranchPending && snappedGridPos != wireStartPos && m_scene) {
        if (m_selectedWireIndex >= 0 && static_cast<size_t>(m_selectedWireIndex) < m_scene->wireCount()) {
            Wire& target = m_scene->wireAt(static_cast<size_t>(m_selectedWireIndex));
            bool hadSrc = target.hasSource();
            bool hadDst = target.hasDest();
            WireEndpoint src = target.getSource();
            WireEndpoint dst = target.getDest();

            Wire wireA, wireB;
            if (m_scene->splitWireAt(static_cast<size_t>(m_selectedWireIndex), wireStartPos, wireA, wireB)) {
                activeWire = Wire();
                if (hadSrc) activeWire.setSource(src.componentId, src.pinIndex);
                else if (hadDst) activeWire.setDest(dst.componentId, dst.pinIndex);

                m_scene->addWires(wireA, wireB);
            }
            baseWirePath = { wireStartPos };
            m_state = InteractionState::DRAWING_WIRE;
            m_hasSelectedSegment = false;
        }
        isMidWireBranchPending = false;
    }

    if (m_state == InteractionState::DRAGGING_GATE && m_draggedComponent) {
        m_draggedComponent->setGridPosition(snappedGridPos);
        lastMouseX = xpos; lastMouseY = ypos;
        return;
    }

    if (m_state == InteractionState::DRAWING_WIRE) {
        if (snappedGridPos != wireStartPos) {
            int dx = snappedGridPos.x - wireStartPos.x;
            int dy = snappedGridPos.y - wireStartPos.y;
            if (!wireAxisLocked) {
                wireAxisXFirst = (std::abs(dx) >= std::abs(dy));
                wireAxisLocked = true;
            }
        }
        else {
            wireAxisLocked = false;
        }

        std::vector<GridCoords> previewPath = baseWirePath;
        if (snappedGridPos != wireStartPos) {
            if (wireStartPos.x != snappedGridPos.x && wireStartPos.y != snappedGridPos.y) {
                if (wireAxisXFirst) previewPath.push_back({ snappedGridPos.x, wireStartPos.y });
                else previewPath.push_back({ wireStartPos.x, snappedGridPos.y });
            }
            previewPath.push_back(snappedGridPos);
        }
        activeWire.setPath(previewPath);
    }

    if (m_state == InteractionState::PANNING) {
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

void Input::process(GLFWwindow* window) {
    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) cancelCurrentAction();

    static bool key1WasPressed = false;
    static bool key2WasPressed = false;
    static bool key3WasPressed = false;
    static bool key4WasPressed = false;
    static bool key5WasPressed = false;
    static bool key6WasPressed = false;
    static bool key7WasPressed = false;
    static bool key8WasPressed = false;

    // Helper lambda to cleanly spawn any 2-input gate without duplicating code
    auto trySpawnGate = [&](GateType type, const std::string& shaderName) {
        if (m_scene && m_state == InteractionState::IDLE) {
            std::vector<PinUI> inPins{
                {PinType::INPUT, 0, PinState::DISCONNECTED, {-2, 1}},
                {PinType::INPUT, 1, PinState::DISCONNECTED, {-2, -1}}
            };
            std::vector<PinUI> outPins{
                {PinType::OUTPUT, 0, PinState::DISCONNECTED, {2, 0}}
            };
            glm::vec2 worldPos = getMouseWorldCoord(window, m_zoom);
            GridCoords gridPos = GridSystem::worldToGrid(worldPos);
            m_scene->addGate(type, gridPos, { 0.2f, 0.2f }, shaderName, inPins, outPins);
        }
        };

    // 1. SPAWN INPUT PIN (Key '1')
    if (glfwGetKey(window, GLFW_KEY_1) == GLFW_PRESS) {
        if (!key1WasPressed && m_scene && m_state == InteractionState::IDLE) {
            glm::vec2 worldPos = getMouseWorldCoord(window, m_zoom);
            GridCoords gridPos = GridSystem::worldToGrid(worldPos);
            m_scene->addInputPin(gridPos, { 0.15f, 0.15f }, "inputPin", false);
        }
        key1WasPressed = true;
    }
    else key1WasPressed = false;

    // 2. SPAWN NOT GATE (Key '2')
    if (glfwGetKey(window, GLFW_KEY_2) == GLFW_PRESS) {
        if (!key2WasPressed && m_scene && m_state == InteractionState::IDLE) {

            // Notice: Only ONE input pin, vertically centered at Y = 0
            std::vector<PinUI> inPins{
                {PinType::INPUT, 0, PinState::DISCONNECTED, {-2, 0}}
            };
            std::vector<PinUI> outPins{
                {PinType::OUTPUT, 0, PinState::DISCONNECTED, {2, 0}}
            };

            glm::vec2 worldPos = getMouseWorldCoord(window, m_zoom);
            GridCoords gridPos = GridSystem::worldToGrid(worldPos);

            m_scene->addGate(GateType::NOT, gridPos, { 0.2f, 0.2f }, "NOTgate", inPins, outPins);
        }
        key2WasPressed = true;
    }
    else key2WasPressed = false;

    // 3. SPAWN AND GATE (Key '3')
    if (glfwGetKey(window, GLFW_KEY_3) == GLFW_PRESS) {
        if (!key3WasPressed) trySpawnGate(GateType::AND, "ANDgate");
        key3WasPressed = true;
    }
    else key3WasPressed = false;

    // 4. SPAWN NAND GATE (Key '4')
    if (glfwGetKey(window, GLFW_KEY_4) == GLFW_PRESS) {
        if (!key4WasPressed) trySpawnGate(GateType::NAND, "ANDgate"); // Reuses AND shader for now
        key4WasPressed = true;
    }
    else key4WasPressed = false;

    // 5. SPAWN OR GATE (Key '5')
    if (glfwGetKey(window, GLFW_KEY_5) == GLFW_PRESS) {
        if (!key5WasPressed) trySpawnGate(GateType::OR, "ORgate");
        key5WasPressed = true;
    }
    else key5WasPressed = false;

    // 6. SPAWN NOR GATE (Key '6')
    if (glfwGetKey(window, GLFW_KEY_6) == GLFW_PRESS) {
        if (!key6WasPressed) trySpawnGate(GateType::NOR, "ORgate"); // Reuses OR shader for now
        key6WasPressed = true;
    }
    else key6WasPressed = false;

    // 7. SPAWN XOR GATE (Key '7')
    if (glfwGetKey(window, GLFW_KEY_7) == GLFW_PRESS) {
        if (!key7WasPressed) trySpawnGate(GateType::XOR, "XORgate");
        key7WasPressed = true;
    }
    else key7WasPressed = false;

    // 8. SPAWN NXOR GATE (Key '8')
    if (glfwGetKey(window, GLFW_KEY_8) == GLFW_PRESS) {
        if (!key8WasPressed) trySpawnGate(GateType::NXOR, "XORgate"); // Reuses XOR shader for now
        key8WasPressed = true;
    }
    else key8WasPressed = false;

    // ==========================================
    // DELETE SELECTED OR HOVERED (Delete or Backspace)
    // ==========================================
    static bool delWasPressed = false;
    if (glfwGetKey(window, GLFW_KEY_DELETE) == GLFW_PRESS || glfwGetKey(window, GLFW_KEY_BACKSPACE) == GLFW_PRESS) {
        if (!delWasPressed && m_scene && m_state == InteractionState::IDLE) {

            // Prioritize explicitly selected items, otherwise delete whatever the mouse is hovering over!
            int compToDelete = m_selectedComponentId != -1 ? m_selectedComponentId : hoveredComponentId;
            int wireToDelete = m_selectedWireIndex != -1 ? m_selectedWireIndex : hoveredWireIndex;

            if (compToDelete != -1) {
                m_scene->removeComponent(compToDelete);
                if (m_selectedComponentId == compToDelete) m_selectedComponentId = -1;
            }
            else if (wireToDelete != -1 && static_cast<size_t>(wireToDelete) < m_scene->wireCount()) {
                Wire& w = m_scene->wireAt(static_cast<size_t>(wireToDelete));

                // 1. Sever the logic connection! If you break a wire, the circuit connection dies.
                if (w.hasSource() && w.hasDest()) {
                    m_scene->disconnectPins(w.getSource().componentId, w.getDest().componentId, w.getDest().pinIndex);
                }

                // 2. Identify exactly which segment to cut
                GridCoords segStart, segEnd;
                bool hasSeg = false;

                if (m_selectedWireIndex != -1 && m_hasSelectedSegment) {
                    segStart = m_selectedSegmentStart; segEnd = m_selectedSegmentEnd;
                    hasSeg = true;
                }
                else if (m_hoveredSegmentValid) {
                    segStart = m_hoveredSegmentStart; segEnd = m_hoveredSegmentEnd;
                    hasSeg = true;
                }

                if (hasSeg) {
                    const auto& path = w.getPath();
                    int cutIdx = -1;

                    // Find where this segment exists in the wire's path array
                    for (size_t i = 0; i < path.size() - 1; ++i) {
                        if ((path[i] == segStart && path[i + 1] == segEnd) || (path[i] == segEnd && path[i + 1] == segStart)) {
                            cutIdx = static_cast<int>(i);
                            break;
                        }
                    }

                    if (cutIdx != -1) {
                        // Split the coordinates into two new paths
                        std::vector<GridCoords> pathA(path.begin(), path.begin() + cutIdx + 1);
                        std::vector<GridCoords> pathB(path.begin() + cutIdx + 1, path.end());

                        WireEndpoint src = w.getSource();
                        WireEndpoint dst = w.getDest();

                        // Destroy the original wire
                        m_scene->removeWire(static_cast<size_t>(wireToDelete));

                        // Spawn Wire A (Retains the Source pin). A wire needs at least 2 points to exist.
                        if (pathA.size() >= 2) {
                            Wire wa; wa.setPath(pathA);
                            if (src.isConnected()) wa.setSource(src.componentId, src.pinIndex);
                            m_scene->commitWire(wa);
                        }

                        // Spawn Wire B (Retains the Dest pin).
                        if (pathB.size() >= 2) {
                            Wire wb; wb.setPath(pathB);
                            if (dst.isConnected()) wb.setDest(dst.componentId, dst.pinIndex);
                            m_scene->commitWire(wb);
                        }
                    }
                    else {
                        m_scene->removeWire(static_cast<size_t>(wireToDelete)); // Fallback
                    }
                }
                else {
                    m_scene->removeWire(static_cast<size_t>(wireToDelete)); // Fallback
                }

                // Clear states
                if (m_selectedWireIndex == wireToDelete) {
                    m_selectedWireIndex = -1;
                    m_hasSelectedSegment = false;
                }
            }

            updateHoverState(window); // Force refresh since the object under the mouse just vanished
        }
        delWasPressed = true;
    }
    else {
        delWasPressed = false;
    }

    updateHoverState(window);
}

void Input::handleScroll(GLFWwindow* window, double xoffset, double yoffset) {
    bool ctrlPressed = (glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS) ||
        (glfwGetKey(window, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS);
    if (ctrlPressed) {
        float zoomSpeed = 0.15f;
        m_zoom += static_cast<float>(yoffset) * zoomSpeed;
        if (m_zoom < 0.2f) m_zoom = 0.2f;
        if (m_zoom > 5.0f) m_zoom = 5.0f;
    }
}

void Input::updateHoverState(GLFWwindow* window)
{
    if (m_state == InteractionState::DRAGGING_GATE || m_state == InteractionState::PANNING) return;
    if (!m_scene) return;

    glm::vec2 currentWorldCoords = getMouseWorldCoord(window, m_zoom);
    mouseGridCoords = GridSystem::worldToGrid(currentWorldCoords);

    hoveredComponentId = -1;
    hoveredPinIndex = -1;
    hoveredWireIndex = -1;
    isHoveredWireStart = false;
    isHoveredWireEnd = false;
    m_hoveredSegmentValid = false; // Reset it every frame

    HitResult hit = m_scene->hitTest(currentWorldCoords, mouseGridCoords);
    switch (hit.type) {
    case HitType::COMPONENT_PIN:
        hoveredComponentId = hit.componentId;
        hoveredPinIndex = hit.pinIndex;
        hoveredPinType = hit.pinType;
        break;
    case HitType::WIRE_END:
        hoveredWireIndex = hit.wireIndex;
        isHoveredWireEnd = true;
        break;
    case HitType::WIRE_START:
        hoveredWireIndex = hit.wireIndex;
        isHoveredWireStart = true;
        break;
    case HitType::WIRE_BODY:
        hoveredWireIndex = hit.wireIndex;
        break;
    case HitType::COMPONENT_BODY:
        hoveredComponentId = hit.componentId;
        break;
    default: break;
    }

    if (hoveredWireIndex != -1 && m_scene) {
        m_hoveredSegmentValid = m_scene->wireAt(static_cast<size_t>(hoveredWireIndex)).getSegmentAt(mouseGridCoords, m_hoveredSegmentStart, m_hoveredSegmentEnd);
    }

}

glm::vec2 Input::getMouseWorldCoord(GLFWwindow* window, float zoom) {
    double mouseX, mouseY;
    glfwGetCursorPos(window, &mouseX, &mouseY);
    int width, height;
    glfwGetWindowSize(window, &width, &height);

    float ndcX = (2.0f * static_cast<float>(mouseX)) / width - 1.0f;
    float ndcY = 1.0f - (2.0f * static_cast<float>(mouseY)) / height;

    float aspectRatio = (height > 0) ? (static_cast<float>(width) / static_cast<float>(height)) : 1.0f;
    float correctedX = ndcX * aspectRatio;
    float correctedY = ndcY;

    float worldX = (correctedX / zoom) + panOffset.x;
    float worldY = (correctedY / zoom) + panOffset.y;

    return glm::vec2(worldX, worldY);
}