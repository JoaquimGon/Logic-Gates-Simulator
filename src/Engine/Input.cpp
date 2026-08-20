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
    // ==========================================
    // RIGHT CLICK (Panning FSM)
    // ==========================================
    if (button == GLFW_MOUSE_BUTTON_RIGHT) {
        if (action == GLFW_PRESS) {
            glfwGetCursorPos(window, &lastMouseX, &lastMouseY);
            m_state = InteractionState::PANNING;
        }
        else if (action == GLFW_RELEASE && m_state == InteractionState::PANNING) {
            m_state = InteractionState::IDLE;
        }
    }

    // ==========================================
    // LEFT CLICK (Interaction FSM)
    // ==========================================
    if (button == GLFW_MOUSE_BUTTON_LEFT) {
        if (action == GLFW_PRESS) {
            glfwGetCursorPos(window, &lastMouseX, &lastMouseY);
            updateHoverState(window);

            bool clickedEmptySpace = true;

            // 1. Clicked a Gate Pin
            if (hoveredGateId != -1 && hoveredPinIndex != -1) {
                clickedEmptySpace = false;
                m_selectedGateId = hoveredGateId;
                m_selectedWireIndex = -1;

                activeWire = Wire();
                if (hoveredPinType == PinType::INPUT) activeWire.setDest(hoveredGateId, hoveredPinIndex);
                else activeWire.setSource(hoveredGateId, hoveredPinIndex);

                activeWire.setState(PinState::DISCONNECTED);
                baseWirePath = { mouseGridCoords };
                wireStartPos = mouseGridCoords;
                wireAxisLocked = false;
                wireAxisXFirst = true;

                m_state = InteractionState::DRAWING_WIRE;
            }
            // 2. Clicked an Existing Wire
            else if (hoveredWireIndex != -1 && m_wires) {
                clickedEmptySpace = false;
                m_selectedGateId = -1;
                m_selectedWireIndex = hoveredWireIndex; // Mark selected IMMEDIATELY

                auto it = m_wires->begin() + hoveredWireIndex;

                if (isHoveredWireStart || isHoveredWireEnd) {
                    activeWire = *it;
                    baseWirePath = it->getPath();

                    if (it->hasSource() && it->hasDest()) {
                        m_wireEvents.push_back(WireEvent{ WireAction::DISCONNECT, it->getSource().gateId, it->getSource().pinIndex, it->getDest().gateId, it->getDest().pinIndex });
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
                    Wire wireA, wireB;
                    if (it->splitAt(mouseGridCoords, wireA, wireB)) {
                        activeWire = Wire();
                        if (it->hasSource()) activeWire.setSource(it->getSource().gateId, it->getSource().pinIndex);
                        else if (it->hasDest()) activeWire.setDest(it->getDest().gateId, it->getDest().pinIndex);

                        m_wires->erase(it);
                        m_wires->push_back(wireA);
                        m_wires->push_back(wireB);
                    }
                    baseWirePath = { mouseGridCoords };
                }

                wireStartPos = mouseGridCoords;
                wireAxisLocked = false;
                wireAxisXFirst = true;
                hoveredWireIndex = -1;
                m_state = InteractionState::DRAWING_WIRE;
            }
            // 3. Clicked a Gate Body
            else {
                glm::vec2 currentWorldCoords = getMouseWorldCoord(window, m_zoom);
                if (m_gates) {
                    for (auto& gate : *m_gates) {
                        glm::vec2 halfSize = gate.getSize() * 0.5f;
                        glm::vec2 delta = currentWorldCoords - gate.getPosition();
                        if (std::abs(delta.x) <= halfSize.x && std::abs(delta.y) <= halfSize.y) {
                            clickedEmptySpace = false;
                            m_draggedGate = &gate;
                            m_selectedGateId = gate.getGateId();
                            m_selectedWireIndex = -1;

                            m_state = InteractionState::DRAGGING_GATE;
                            break;
                        }
                    }
                }
            }

            // 4. Clicked Empty Space
            if (clickedEmptySpace) {
                m_selectedGateId = -1;
                m_selectedWireIndex = -1;
                activeWire = Wire();
                baseWirePath = { mouseGridCoords };
                wireStartPos = mouseGridCoords;
                wireAxisLocked = false;
                wireAxisXFirst = true;

                m_state = InteractionState::DRAWING_WIRE;
            }
        }
        else if (action == GLFW_RELEASE) {

            // ==========================================
            // RELEASE: DRAGGING GATE
            // ==========================================
            if (m_state == InteractionState::DRAGGING_GATE && m_draggedGate && m_wires) {
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
                m_draggedGate = nullptr;
                m_state = InteractionState::IDLE;
            }

            // ==========================================
            // RELEASE: DRAWING WIRE
            // ==========================================
            else if (m_state == InteractionState::DRAWING_WIRE) {

                // IF NO DRAG OCCURRED: Restore the wire safely and keep it selected
                if (mouseGridCoords == wireStartPos && activeWire.getPath().size() <= 1) {
                    if (m_wires && (activeWire.hasSource() || activeWire.hasDest() || activeWire.getPath().size() > 0)) {
                        m_wires->push_back(activeWire);
                        m_selectedWireIndex = static_cast<int>(m_wires->size() - 1);
                    }
                    m_state = InteractionState::IDLE;
                    return; // Skip connection logic!
                }

                updateHoverState(window);

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
                        if (hoveredPinType == PinType::INPUT && !activeWire.hasDest()) activeWire.setDest(hoveredGateId, hoveredPinIndex);
                        else if (hoveredPinType == PinType::OUTPUT && !activeWire.hasSource()) activeWire.setSource(hoveredGateId, hoveredPinIndex);

                        if (activeWire.hasSource() && activeWire.hasDest()) {
                            m_wireEvents.push_back(WireEvent{ WireAction::CONNECT, activeWire.getSource().gateId, activeWire.getSource().pinIndex, activeWire.getDest().gateId, activeWire.getDest().pinIndex });
                        }
                    }
                }
                else if (hoveredWireIndex != -1 && m_wires) {
                    auto targetWireIt = m_wires->begin() + hoveredWireIndex;
                    Wire wireA, wireB;

                    if (targetWireIt->splitAt(mouseGridCoords, wireA, wireB)) {
                        bool activeHasSrc = activeWire.hasSource();
                        bool targetHasSrc = targetWireIt->hasSource();
                        bool activeHasDst = activeWire.hasDest();
                        bool targetHasDst = targetWireIt->hasDest();

                        WireEndpoint src = activeHasSrc ? activeWire.getSource() : targetWireIt->getSource();
                        WireEndpoint dst = activeHasDst ? activeWire.getDest() : targetWireIt->getDest();

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

                        if ((activeHasSrc && targetHasDst) || (activeHasDst && targetHasSrc)) {
                            m_wireEvents.push_back(WireEvent{ WireAction::CONNECT, src.gateId, src.pinIndex, dst.gateId, dst.pinIndex });
                        }

                        m_wires->erase(targetWireIt);
                        m_wires->push_back(wireA);
                        m_wires->push_back(wireB);
                    }
                }

                if (m_wires && activeWire.getPath().size() > 1) {
                    m_wires->push_back(activeWire);
                    m_selectedWireIndex = static_cast<int>(m_wires->size() - 1);
                }

                m_state = InteractionState::IDLE;
            }
        }
    }
}

void Input::handleCursorPos(GLFWwindow* window, double xpos, double ypos)
{
    glm::vec2 worldCoords = getMouseWorldCoord(window, m_zoom);
    GridCoords snappedGridPos = GridSystem::worldToGrid(worldCoords);

    // ==========================================
    // FSM CURSOR UPDATES
    // ==========================================
    if (m_state == InteractionState::DRAGGING_GATE && m_draggedGate) {
        m_draggedGate->setGridPosition(snappedGridPos);
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
    // Don't shift focus if we are busy handling an FSM state
    if (m_state == InteractionState::DRAGGING_GATE || m_state == InteractionState::PANNING) return;

    glm::vec2 currentWorldCoords = getMouseWorldCoord(window, m_zoom);
    mouseGridCoords = GridSystem::worldToGrid(currentWorldCoords);

    hoveredGateId = -1;
    hoveredPinIndex = -1;
    hoveredWireIndex = -1;
    isHoveredWireStart = false;
    isHoveredWireEnd = false;

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