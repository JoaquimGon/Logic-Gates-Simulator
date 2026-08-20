#include "..\..\include\Engine\Input.h"
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

void Input::handleMouseButton(GLFWwindow* window, int button, int action, int mods)
{
    if (button == GLFW_MOUSE_BUTTON_RIGHT) {
        if (action == GLFW_PRESS) {
            glfwGetCursorPos(window, &lastMouseX, &lastMouseY);
            isDragging = true;
        }
        else if (action == GLFW_RELEASE) {
            isDragging = false;
        }
    }

    if (button == GLFW_MOUSE_BUTTON_LEFT) {
        if (action == GLFW_PRESS) {
            glfwGetCursorPos(window, &lastMouseX, &lastMouseY);
            updateHoverState(window);

            // 1. Clicked a Gate Pin
            if (hoveredGateId != -1 && hoveredPinIndex != -1) {
                activeWire = Wire();

                if (hoveredPinType == PinType::INPUT) {
                    activeWire.setDest(hoveredGateId, hoveredPinIndex);
                }
                else {
                    activeWire.setSource(hoveredGateId, hoveredPinIndex);
                }

                activeWire.setState(PinState::DISCONNECTED);
                baseWirePath = { mouseGridCoords };
                wireStartPos = mouseGridCoords;
                isDrawingWire = true;
                wireAxisLocked = false;
                wireAxisXFirst = true;
            }
            // 2. Clicked an Existing Wire
            else if (hoveredWireIndex != -1 && m_wires) {
                auto it = m_wires->begin() + hoveredWireIndex;

                if (isHoveredWireStart || isHoveredWireEnd) {
                    activeWire = *it;
                    baseWirePath = it->getPath();

                    if (it->hasSource() && it->hasDest()) {
                        m_wireEvents.push_back(WireEvent{
                            WireAction::DISCONNECT,
                            it->getSource().gateId, it->getSource().pinIndex,
                            it->getDest().gateId, it->getDest().pinIndex
                            });
                    }

                    if (isHoveredWireStart) {
                        std::reverse(baseWirePath.begin(), baseWirePath.end());
                        activeWire.disconnectSource();
                    }
                    else {
                        activeWire.disconnectDest();
                    }

                    m_wires->erase(it);
                }
                else {
                    // ==========================================
                    // MID-WIRE BRANCH CREATION
                    // ==========================================
                    Wire wireA, wireB;
                    if (it->splitAt(mouseGridCoords, wireA, wireB)) {
                        activeWire = Wire();

                        // Inherit the Power (Source) or the Destination to the new branch!
                        if (it->hasSource()) {
                            activeWire.setSource(it->getSource().gateId, it->getSource().pinIndex);
                        }
                        else if (it->hasDest()) {
                            activeWire.setDest(it->getDest().gateId, it->getDest().pinIndex);
                        }

                        m_wires->erase(it);
                        m_wires->push_back(wireA);
                        m_wires->push_back(wireB);
                    }
                    baseWirePath = { mouseGridCoords };
                }

                wireStartPos = mouseGridCoords;
                isDrawingWire = true;
                wireAxisLocked = false;
                wireAxisXFirst = true;
                hoveredWireIndex = -1;
            }
            // 3. Clicked a Gate Body
            else {
                glm::vec2 currentWorldCoords = getMouseWorldCoord(window, m_zoom);
                bool clickedGate = false;
                if (m_gates) {
                    for (auto& gate : *m_gates) {
                        glm::vec2 halfSize = gate.getSize() * 0.5f;
                        glm::vec2 delta = currentWorldCoords - gate.getPosition();
                        if (std::abs(delta.x) <= halfSize.x && std::abs(delta.y) <= halfSize.y) {
                            m_draggedGate = &gate;
                            isDraggingGate = true;
                            clickedGate = true;
                            break;
                        }
                    }
                }

                // 4. Clicked Empty Space
                if (!clickedGate) {
                    activeWire = Wire();
                    baseWirePath = { mouseGridCoords };
                    wireStartPos = mouseGridCoords;
                    isDrawingWire = true;
                    wireAxisLocked = false;
                    wireAxisXFirst = true;
                }
            }
        }
        else if (action == GLFW_RELEASE) {

            // ==========================================
            // Gate Drop Connection Logic (Unchanged)
            // ==========================================
            if (isDraggingGate && m_draggedGate && m_wires) {
                int gateId = m_draggedGate->getGateId();

                for (const auto& pin : m_draggedGate->m_inputs) {
                    GridCoords pinPos = m_draggedGate->getAbsolutePinGridPos(pin);
                    for (size_t i = 0; i < m_wires->size(); ++i) {
                        auto& wire = (*m_wires)[i];
                        if (wire.getPath().empty()) continue;

                        if (!wire.hasDest() && (wire.getPath().front() == pinPos || wire.getPath().back() == pinPos)) {
                            wire.setDest(gateId, static_cast<int>(pin.pin_index));
                            if (wire.hasSource()) {
                                m_wireEvents.push_back(WireEvent{ WireAction::CONNECT, wire.getSource().gateId, wire.getSource().pinIndex, wire.getDest().gateId, wire.getDest().pinIndex });
                            }
                            break;
                        }
                        else if (wire.containsPoint(pinPos)) {
                            Wire wireA, wireB;
                            if (wire.splitAt(pinPos, wireA, wireB)) {
                                wireA.setDest(gateId, static_cast<int>(pin.pin_index));
                                if (wireA.hasSource()) {
                                    m_wireEvents.push_back(WireEvent{ WireAction::CONNECT, wireA.getSource().gateId, wireA.getSource().pinIndex, gateId, static_cast<int>(pin.pin_index) });
                                }
                                m_wires->erase(m_wires->begin() + i);
                                m_wires->push_back(wireA);
                                m_wires->push_back(wireB);
                            }
                            break;
                        }
                    }
                }

                for (const auto& pin : m_draggedGate->m_outputs) {
                    GridCoords pinPos = m_draggedGate->getAbsolutePinGridPos(pin);
                    for (size_t i = 0; i < m_wires->size(); ++i) {
                        auto& wire = (*m_wires)[i];
                        if (wire.getPath().empty()) continue;

                        if (!wire.hasSource() && (wire.getPath().front() == pinPos || wire.getPath().back() == pinPos)) {
                            wire.setSource(gateId, static_cast<int>(pin.pin_index));
                            if (wire.hasDest()) {
                                m_wireEvents.push_back(WireEvent{ WireAction::CONNECT, wire.getSource().gateId, wire.getSource().pinIndex, wire.getDest().gateId, wire.getDest().pinIndex });
                            }
                            break;
                        }
                        else if (wire.containsPoint(pinPos)) {
                            Wire wireA, wireB;
                            if (wire.splitAt(pinPos, wireA, wireB)) {
                                wireB.setSource(gateId, static_cast<int>(pin.pin_index));
                                if (wireB.hasDest()) {
                                    m_wireEvents.push_back(WireEvent{ WireAction::CONNECT, gateId, static_cast<int>(pin.pin_index), wireB.getDest().gateId, wireB.getDest().pinIndex });
                                }
                                m_wires->erase(m_wires->begin() + i);
                                m_wires->push_back(wireA);
                                m_wires->push_back(wireB);
                            }
                            break;
                        }
                    }
                }
            }

            isDraggingGate = false;
            m_draggedGate = nullptr;

            // ==========================================
            // Wire Drop Connection Logic
            // ==========================================
            if (isDrawingWire) {
                updateHoverState(window);

                // A. Dropped on a Pin
                if (hoveredGateId != -1 && hoveredPinIndex != -1) {
                    bool isSamePin = (hoveredPinType == PinType::INPUT && activeWire.hasDest() &&
                        activeWire.getDest().gateId == hoveredGateId && activeWire.getDest().pinIndex == hoveredPinIndex) ||
                        (hoveredPinType == PinType::OUTPUT && activeWire.hasSource() &&
                            activeWire.getSource().gateId == hoveredGateId && activeWire.getSource().pinIndex == hoveredPinIndex);

                    if (isSamePin) {
                        if (hoveredPinType == PinType::INPUT) activeWire.disconnectDest();
                        else activeWire.disconnectSource();
                    }
                    else {
                        if (hoveredPinType == PinType::INPUT && !activeWire.hasDest()) {
                            activeWire.setDest(hoveredGateId, hoveredPinIndex);
                        }
                        else if (hoveredPinType == PinType::OUTPUT && !activeWire.hasSource()) {
                            activeWire.setSource(hoveredGateId, hoveredPinIndex);
                        }

                        if (activeWire.hasSource() && activeWire.hasDest()) {
                            m_wireEvents.push_back(WireEvent{
                                WireAction::CONNECT,
                                activeWire.getSource().gateId, activeWire.getSource().pinIndex,
                                activeWire.getDest().gateId, activeWire.getDest().pinIndex
                                });
                        }
                    }
                }
                // B. Dropped on an Existing Wire (Intersection)
                else if (hoveredWireIndex != -1 && m_wires) {
                    auto targetWireIt = m_wires->begin() + hoveredWireIndex;
                    Wire wireA, wireB;

                    if (targetWireIt->splitAt(mouseGridCoords, wireA, wireB)) {

                        // ==========================================
                        // FIX: SHARE THE LOGIC CONNECTIONS!
                        // ==========================================
                        bool activeHasSrc = activeWire.hasSource();
                        bool targetHasSrc = targetWireIt->hasSource();
                        bool activeHasDst = activeWire.hasDest();
                        bool targetHasDst = targetWireIt->hasDest();

                        WireEndpoint src = activeHasSrc ? activeWire.getSource() : targetWireIt->getSource();
                        WireEndpoint dst = activeHasDst ? activeWire.getDest() : targetWireIt->getDest();

                        // Apply to all 3 pieces so they act as "one single wire"
                        if (activeHasSrc || targetHasSrc) {
                            activeWire.setSource(src.gateId, src.pinIndex);
                            wireA.setSource(src.gateId, src.pinIndex);
                            wireB.setSource(src.gateId, src.pinIndex);
                        }
                        if (activeHasDst || targetHasDst) {
                            activeWire.setDest(dst.gateId, dst.pinIndex);
                            wireA.setDest(dst.gateId, dst.pinIndex);
                            wireB.setDest(dst.gateId, dst.pinIndex);
                        }

                        // Only fire a connect event if this specific drop action COMPLETED the circuit!
                        if ((activeHasSrc && targetHasDst) || (activeHasDst && targetHasSrc)) {
                            m_wireEvents.push_back(WireEvent{
                                WireAction::CONNECT,
                                src.gateId, src.pinIndex,
                                dst.gateId, dst.pinIndex
                                });
                        }

                        m_wires->erase(targetWireIt);
                        m_wires->push_back(wireA);
                        m_wires->push_back(wireB);
                    }
                }

                if (m_wires && activeWire.getPath().size() > 1) {
                    m_wires->push_back(activeWire);
                }
                isDrawingWire = false;
            }
        }
    }
}

void Input::handleCursorPos(GLFWwindow* window, double xpos, double ypos)
{
    glm::vec2 worldCoords = getMouseWorldCoord(window, m_zoom);
    GridCoords snappedGridPos = GridSystem::worldToGrid(worldCoords);

    if (isDraggingGate && m_draggedGate) {
        m_draggedGate->setGridPosition(snappedGridPos);
        lastMouseX = xpos; lastMouseY = ypos;
        return;
    }

    if (isDrawingWire) {
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
                if (wireAxisXFirst) {
                    previewPath.push_back({ snappedGridPos.x, wireStartPos.y });
                }
                else {
                    previewPath.push_back({ wireStartPos.x, snappedGridPos.y });
                }
            }
            previewPath.push_back(snappedGridPos);
        }

        activeWire.setPath(previewPath);
    }

    if (isDragging) {
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
    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
        glfwSetWindowShouldClose(window, true);

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
    if (isDraggingGate || isDragging) return;

    glm::vec2 currentWorldCoords = getMouseWorldCoord(window, m_zoom);
    mouseGridCoords = GridSystem::worldToGrid(currentWorldCoords);

    hoveredGateId = -1;
    hoveredPinIndex = -1;
    hoveredWireIndex = -1;
    isHoveredWireStart = false;
    isHoveredWireEnd = false;

    // Check Pins First
    if (m_gates) {
        for (size_t gateId = 0; gateId < m_gates->size(); ++gateId) {
            auto& gate = (*m_gates)[gateId];

            for (const auto& pin : gate.m_inputs) {
                if (mouseGridCoords == gate.getAbsolutePinGridPos(pin)) {
                    hoveredGateId = static_cast<int>(gateId);
                    hoveredPinIndex = static_cast<int>(pin.pin_index);
                    hoveredPinType = PinType::INPUT;
                    return;
                }
            }

            for (const auto& pin : gate.m_outputs) {
                if (mouseGridCoords == gate.getAbsolutePinGridPos(pin)) {
                    hoveredGateId = static_cast<int>(gateId);
                    hoveredPinIndex = static_cast<int>(pin.pin_index);
                    hoveredPinType = PinType::OUTPUT;
                    return;
                }
            }
        }
    }

    // Check Wires (Endpoints & Mid-Wire Intersections)
    if (m_wires) {
        for (size_t i = 0; i < m_wires->size(); ++i) {
            const auto& wire = (*m_wires)[i];
            const auto& path = wire.getPath();
            if (path.empty()) continue;

            if (mouseGridCoords == path.back()) {
                hoveredWireIndex = static_cast<int>(i);
                isHoveredWireEnd = true;
                return;
            }
            else if (mouseGridCoords == path.front()) {
                hoveredWireIndex = static_cast<int>(i);
                isHoveredWireStart = true;
                return;
            }
            else if (wire.containsPoint(mouseGridCoords)) {
                hoveredWireIndex = static_cast<int>(i);
                return;
            }
        }
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