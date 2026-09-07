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

            if (hoveredPinComponentId != -1 && hoveredPinIndex != -1) {
                m_selectedComponentId = hoveredComponentId;

                activeWire = Wire();
                if (hoveredPinType == PinType::INPUT) activeWire.setDest(hoveredPinComponentId, hoveredPinIndex);
                else activeWire.setSource(hoveredPinComponentId, hoveredPinIndex);

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
                    m_dragStartPos = m_draggedComponent->getGridPosition();
                    m_state = InteractionState::DRAGGING_GATE;
                }
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

                // NEW: Validate the placement
                if (m_scene->checkOverlap(m_draggedComponent->getComponentId())) {
                    // Overlap detected! Snap the gate back to where it started.
                    m_draggedComponent->setGridPosition(m_dragStartPos);
                }

                // Finalize placement (either the new spot, or the reverted spot)
                m_scene->reconnectWiresToComponent(m_draggedComponent->getComponentId());
                m_draggedComponent = nullptr;
                m_state = InteractionState::IDLE;
            }
            else if (m_state == InteractionState::DRAWING_WIRE) {
                // NEW: Prevent degenerate invisible wires from polluting the math!
                if (activeWire.getPath().size() <= 1) {
                    m_state = InteractionState::IDLE;
                    activeWire = Wire(); // Destroy it
                    return;
                }

                updateHoverState(window);

                if (hoveredPinComponentId != -1 && hoveredPinIndex != -1) {
                    bool isSamePin = (hoveredPinType == PinType::INPUT && activeWire.hasDest() &&
                        activeWire.getDest().componentId == hoveredPinComponentId && activeWire.getDest().pinIndex == hoveredPinIndex) ||
                        (hoveredPinType == PinType::OUTPUT && activeWire.hasSource() &&
                            activeWire.getSource().componentId == hoveredPinComponentId && activeWire.getSource().pinIndex == hoveredPinIndex);

                    if (isSamePin) {
                        if (hoveredPinType == PinType::INPUT) activeWire.disconnectDest();
                        else activeWire.disconnectSource();
                    }
                    else {
                        if (hoveredPinType == PinType::INPUT && !activeWire.hasDest()) activeWire.setDest(hoveredPinComponentId, hoveredPinIndex);
                        else if (hoveredPinType == PinType::OUTPUT && !activeWire.hasSource()) activeWire.setSource(hoveredPinComponentId, hoveredPinIndex);

                        if (activeWire.hasSource() && activeWire.hasDest()) {
                            m_scene->connectPins(activeWire.getSource().componentId, activeWire.getDest().componentId, activeWire.getDest().pinIndex);
                        }
                    }
                }
                else if (hoveredWireIndex != -1) {
                    Wire& targetWire = m_scene->wireAt(static_cast<size_t>(hoveredWireIndex));

                    // Did we drop exactly on an endpoint?
                    bool hitEndpoint = (!targetWire.getPath().empty()) &&
                        (mouseGridCoords == targetWire.getPath().front() || mouseGridCoords == targetWire.getPath().back());

                    if (hitEndpoint) {
                        // MERGE LOGIC (Do not split the target wire!)
                        bool activeHasSrc = activeWire.hasSource();
                        bool activeHasDst = activeWire.hasDest();
                        bool targetHasSrc = targetWire.hasSource();
                        bool targetHasDst = targetWire.hasDest();

                        WireEndpoint src = activeHasSrc ? activeWire.getSource() : targetWire.getSource();
                        WireEndpoint dst = activeHasDst ? activeWire.getDest() : targetWire.getDest();

                        if (activeHasSrc || targetHasSrc) {
                            activeWire.setSource(src.componentId, src.pinIndex);
                            targetWire.setSource(src.componentId, src.pinIndex);
                        }
                        if (activeHasDst || targetHasDst) {
                            activeWire.setDest(dst.componentId, dst.pinIndex);
                            targetWire.setDest(dst.componentId, dst.pinIndex);
                        }

                        if ((activeHasSrc && targetHasDst) || (activeHasDst && targetHasSrc)) {
                            m_scene->connectPins(src.componentId, dst.componentId, dst.pinIndex);
                        }
                    }
                    else {
                        // SPLIT LOGIC (We dropped on the middle of a wire)
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

            PinState branchState = target.getState();

            bool hitEndpoint = (!target.getPath().empty()) &&
                (wireStartPos == target.getPath().front() || wireStartPos == target.getPath().back());

            if (!hitEndpoint) {
                Wire wireA, wireB;
                if (m_scene->splitWireAt(static_cast<size_t>(m_selectedWireIndex), wireStartPos, wireA, wireB)) {
                    m_scene->addWires(wireA, wireB);
                }
            }

            activeWire = Wire();
            activeWire.setState(branchState);
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
                GridCoords corner = wireAxisXFirst
                    ? GridCoords{ snappedGridPos.x, wireStartPos.y }
                : GridCoords{ wireStartPos.x, snappedGridPos.y };

                GridCoords leg1End = m_scene ? m_scene->clipSegmentAgainstWires(wireStartPos, corner) : corner;
                if (leg1End != wireStartPos) previewPath.push_back(leg1End);

                if (leg1End == corner) {
                    GridCoords leg2End = m_scene ? m_scene->clipSegmentAgainstWires(corner, snappedGridPos) : snappedGridPos;
                    if (leg2End != corner) previewPath.push_back(leg2End);
                }
            }
            else {
                GridCoords legEnd = m_scene ? m_scene->clipSegmentAgainstWires(wireStartPos, snappedGridPos) : snappedGridPos;
                if (legEnd != wireStartPos) previewPath.push_back(legEnd);
            }
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

            int newId = m_scene->addGate(type, gridPos, { 0.2f, 0.2f }, shaderName, inPins, outPins);

            if (ComponentView* cv = m_scene->getComponentView(newId)) {
                while (m_scene->checkOverlap(newId)) {
                    gridPos.x += 1;
                    gridPos.y -= 1;
                    cv->setGridPosition(gridPos);
                }
            }
        }
        };

    // 1. SPAWN INPUT PIN (Key '1')
    if (glfwGetKey(window, GLFW_KEY_1) == GLFW_PRESS) {
        if (!key1WasPressed && m_scene && m_state == InteractionState::IDLE) {
            glm::vec2 worldPos = getMouseWorldCoord(window, m_zoom);
            GridCoords gridPos = GridSystem::worldToGrid(worldPos);

            int newId = m_scene->addInputPin(gridPos, { 0.15f, 0.15f }, "inputPin", false);

            if (ComponentView* cv = m_scene->getComponentView(newId)) {
                while (m_scene->checkOverlap(newId)) {
                    gridPos.x += 1;
                    gridPos.y -= 1;
                    cv->setGridPosition(gridPos);
                }
            }
        }
        key1WasPressed = true;
    }
    else key1WasPressed = false;

    // 2. SPAWN NOT GATE (Key '2')
    if (glfwGetKey(window, GLFW_KEY_2) == GLFW_PRESS) {
        if (!key2WasPressed && m_scene && m_state == InteractionState::IDLE) {

            std::vector<PinUI> inPins{
                {PinType::INPUT, 0, PinState::DISCONNECTED, {-2, 0}}
            };
            std::vector<PinUI> outPins{
                {PinType::OUTPUT, 0, PinState::DISCONNECTED, {2, 0}}
            };

            glm::vec2 worldPos = getMouseWorldCoord(window, m_zoom);
            GridCoords gridPos = GridSystem::worldToGrid(worldPos);

            int newId = m_scene->addGate(GateType::NOT, gridPos, { 0.2f, 0.2f }, "NOTgate", inPins, outPins);

            if (ComponentView* cv = m_scene->getComponentView(newId)) {
                while (m_scene->checkOverlap(newId)) {
                    gridPos.x += 1;
                    gridPos.y -= 1;
                    cv->setGridPosition(gridPos);
                }
            }
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
    hoveredPinComponentId = -1;
    hoveredWireIndex = -1;
    isHoveredWireStart = false;
    isHoveredWireEnd = false;
    m_hoveredSegmentValid = false; // Reset it every frame

    HitResult hit = m_scene->hitTest(currentWorldCoords, mouseGridCoords);
    switch (hit.type) {
    case HitType::COMPONENT_PIN:
        hoveredPinComponentId = hit.componentId;
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