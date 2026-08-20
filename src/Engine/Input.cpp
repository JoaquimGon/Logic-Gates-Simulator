#include "..\..\include\Engine\Input.h"

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
            isDragging = false; // Fix: This should just be isDragging = false; 
        }
    }

    if (button == GLFW_MOUSE_BUTTON_LEFT) {
        if (action == GLFW_PRESS) {
            glfwGetCursorPos(window, &lastMouseX, &lastMouseY);
            // Ensure hover state is perfectly accurate at the exact moment of clicking
            updateHoverState(window);

            // 1. Did we click a Pin?
            if (hoveredGateId != -1 && hoveredPinIndex != -1) {
                activeWire = Wire();
                activeWire.setSource(hoveredGateId, hoveredPinIndex);
                activeWire.setState(PinState::DISCONNECTED);

                baseWirePath = { mouseGridCoords };
                wireStartPos = mouseGridCoords;
                isDrawingWire = true;
                wireAxisLocked = false;
                wireAxisXFirst = true;
            }
            // 2. Did we click a Wire Endpoint?
            else if (hoveredWireIndex != -1 && m_wires) {
                auto it = m_wires->begin() + hoveredWireIndex;
                baseWirePath = it->getPath();

                // If this wire was fully connected to two things, we are breaking the connection!
                if (it->hasSource() && it->hasDest()) {
                    m_wireEvents.push_back({ WireAction::DISCONNECT,
                        it->getSource().gateId, it->getSource().pinIndex,
                        it->getDest().gateId, it->getDest().pinIndex });
                }

                if (isHoveredWireStart) {
                    std::reverse(baseWirePath.begin(), baseWirePath.end());
                    activeWire = *it;
                    activeWire.disconnectSource(); // Ripped it out of its source!
                }
                else {
                    activeWire = *it;
                    activeWire.disconnectDest(); // Ripped it out of its destination!
                }

                m_wires->erase(it); // Remove from master list while drawing
                wireStartPos = mouseGridCoords;
                isDrawingWire = true;
                wireAxisLocked = false;
                wireAxisXFirst = true;
                hoveredWireIndex = -1;
            }
            // 3. Did we click a Gate Body?
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

                // 4. Clicked Empty Space (Start floating wire)
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
            isDraggingGate = false;
            m_draggedGate = nullptr;

            if (isDrawingWire) {
                updateHoverState(window);

                if (hoveredGateId != -1 && hoveredPinIndex != -1) {

                    // FIX: Check if the destination is identical to the source!
                    bool isSamePin = (activeWire.hasSource() &&
                        activeWire.getSource().gateId == hoveredGateId &&
                        activeWire.getSource().pinIndex == hoveredPinIndex);

                    if (isSamePin) {
                        activeWire.disconnectDest(); // Cancel the connection
                    }
                    else {
                        activeWire.setDest(hoveredGateId, hoveredPinIndex); // Visual connect!

                        if (activeWire.hasSource()) {
                            m_wireEvents.push_back({ WireAction::CONNECT,
                                activeWire.getSource().gateId, activeWire.getSource().pinIndex,
                                activeWire.getDest().gateId, activeWire.getDest().pinIndex });
                        }
                    }
                }
                else {
                    activeWire.disconnectDest();
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

    // 1. Handle Gate Dragging
    if (isDraggingGate && m_draggedGate) {
        m_draggedGate->setGridPosition(snappedGridPos);
        lastMouseX = xpos; lastMouseY = ypos;
        return;
    }

    // 2. Handle Wire Drawing (The L-Shape Logic)
    if (isDrawingWire) {
        if (snappedGridPos != wireStartPos) {
            int dx = snappedGridPos.x - wireStartPos.x;
            int dy = snappedGridPos.y - wireStartPos.y;

            if (!wireAxisLocked) {
                if (std::abs(dx) >= std::abs(dy)) {
                    wireAxisXFirst = true;
                    wireAxisLocked = true;
                }
                else {
                    wireAxisXFirst = false;
                    wireAxisLocked = true;
                }
            }
        }
        else {
            wireAxisLocked = false; // Reset behavior if returning to origin
        }

        // Start with the established wire history
        std::vector<GridCoords> previewPath = baseWirePath;

        // Append the new L-Shape preview
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

    // 3. Handle Camera Panning
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
    // Check hitboxes even while drawing wire!
    if (isDraggingGate || isDragging) return;

    glm::vec2 currentWorldCoords = getMouseWorldCoord(window, m_zoom);
    mouseGridCoords = GridSystem::worldToGrid(currentWorldCoords);

    // Reset states
    hoveredGateId = -1;
    hoveredPinIndex = -1;
    hoveredWireIndex = -1;
    isHoveredWireStart = false;

    // 1. Check if hovering a Pin (Now checking BOTH Inputs and Outputs!)
    if (m_gates) {
        for (size_t gateId = 0; gateId < m_gates->size(); ++gateId) {
            auto& gate = (*m_gates)[gateId];

            // Check Input Pins
            for (const auto& pin : gate.m_inputs) {
                if (mouseGridCoords == gate.getAbsolutePinGridPos(pin)) {
                    hoveredGateId = static_cast<int>(gateId);
                    hoveredPinIndex = pin.pin_index;
                    hoveredPinType = PinType::INPUT;
                    return;
                }
            }

            // Check Output Pins
            for (const auto& pin : gate.m_outputs) {
                if (mouseGridCoords == gate.getAbsolutePinGridPos(pin)) {
                    hoveredGateId = static_cast<int>(gateId);
                    hoveredPinIndex = pin.pin_index;
                    hoveredPinType = PinType::OUTPUT;
                    return;
                }
            }
        }
    }

    // 2. Check if hovering a Wire Endpoint
    if (m_wires) {
        for (size_t i = 0; i < m_wires->size(); ++i) {
            const auto& path = (*m_wires)[i].getPath();
            if (path.empty()) continue;

            if (mouseGridCoords == path.back()) {
                hoveredWireIndex = static_cast<int>(i);
                isHoveredWireStart = false;
                return;
            }
            else if (mouseGridCoords == path.front()) {
                hoveredWireIndex = static_cast<int>(i);
                isHoveredWireStart = true;
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