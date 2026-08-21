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

void Input::cancelCurrentAction() {
    if (m_state == InteractionState::DRAWING_WIRE) {
        if (m_scene && !isMidWireBranchPending &&
            (activeWire.hasSource() || activeWire.hasDest() || activeWire.getPath().size() > 1)) {
            m_scene->commitWire(activeWire);
        }
        activeWire = Wire();
        baseWirePath.clear();
        isMidWireBranchPending = false;
    }

    m_draggedGate = nullptr;
    m_selectedGateId = -1;
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
            m_selectedGateId = -1;
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

            m_selectedGateId = -1;
            m_selectedWireIndex = -1;
            m_hasSelectedSegment = false;
            isMidWireBranchPending = false;

            if (hoveredGateId != -1 && hoveredPinIndex != -1) {
                m_selectedGateId = hoveredGateId;

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
            else if (hoveredWireIndex != -1 && (isHoveredWireStart || isHoveredWireEnd)) {
                m_selectedWireIndex = hoveredWireIndex;

                Wire picked = m_scene->extractWire(static_cast<size_t>(hoveredWireIndex));
                m_hasSelectedSegment = picked.getSegmentAt(mouseGridCoords, m_selectedSegmentStart, m_selectedSegmentEnd);

                activeWire = picked;
                baseWirePath = picked.getPath();

                if (picked.hasSource() && picked.hasDest()) {
                    m_scene->disconnectPins(picked.getSource().gateId, picked.getDest().gateId, picked.getDest().pinIndex);
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
            else if (hoveredGateId != -1) {
                m_selectedGateId = hoveredGateId;
                m_draggedGate = m_scene->getGateView(hoveredGateId);
                m_state = InteractionState::DRAGGING_GATE;
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

            if (m_state == InteractionState::DRAGGING_GATE && m_draggedGate) {
                m_scene->reconnectWiresToGate(m_draggedGate->getGateId());
                m_draggedGate = nullptr;
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
                            m_scene->connectPins(activeWire.getSource().gateId, activeWire.getDest().gateId, activeWire.getDest().pinIndex);
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
                            m_scene->connectPins(src.gateId, dst.gateId, dst.pinIndex);
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
                if (hadSrc) activeWire.setSource(src.gateId, src.pinIndex);
                else if (hadDst) activeWire.setDest(dst.gateId, dst.pinIndex);

                m_scene->addWires(wireA, wireB);
            }
            baseWirePath = { wireStartPos };
            m_state = InteractionState::DRAWING_WIRE;
            m_hasSelectedSegment = false;
        }
        isMidWireBranchPending = false;
    }

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
    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) cancelCurrentAction();
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

    hoveredGateId = -1;
    hoveredPinIndex = -1;
    hoveredWireIndex = -1;
    isHoveredWireStart = false;
    isHoveredWireEnd = false;

    HitResult hit = m_scene->hitTest(currentWorldCoords, mouseGridCoords);
    switch (hit.type) {
    case HitType::GATE_PIN:
        hoveredGateId = hit.gateId;
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
    case HitType::GATE_BODY:
        hoveredGateId = hit.gateId;
        break;
    default: break;
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