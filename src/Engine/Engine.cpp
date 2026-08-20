#include "..\..\include\Engine\Engine.h"
#include <iostream>

Engine::Engine(std::string windowName, int windowWidth, int windowHeight)
{
    m_windowName = windowName;
    m_windowWidth = windowWidth;
    m_windowHeight = windowHeight;
}

int Engine::init()
{
    // ==========================================
    // glfw Configuration
    // ==========================================
    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    window = glfwCreateWindow(m_windowWidth, m_windowHeight, m_windowName.c_str(), NULL, NULL);
    if (window == NULL)
    {
        std::cout << "Failed to create GLFW window" << std::endl;
        glfwTerminate();
        return -1;
    }

    glfwMakeContextCurrent(window);
    glfwSetFramebufferSizeCallback(window, Engine::resizeWindow);
    glfwSetWindowUserPointer(window, &input);

    // Mouse callbacks
    glfwSetMouseButtonCallback(window, Input::mouseButtonCallback);
    glfwSetCursorPosCallback(window, Input::cursorPositionCallback);
    glfwSetScrollCallback(window, Input::scrollCallback);

    // ==========================================
    // glad Configuration
    // ==========================================
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
    {
        std::cout << "Failed to initialize GLAD" << std::endl;
        return -1;
    }

    // ==========================================
    // Initialize Graphics/Renderer
    // ==========================================
    m_renderer.init();

    return 0;
}

void Engine::run()
{
    Circuit circuit;
    int gate0_id = circuit.addGate(GateType::AND, false);
    int gate1_id = circuit.addGate(GateType::AND, false);

    std::vector<GateView> gatesViews;
    std::vector<Wire> wires;

    // Setup Test Data
    std::vector<PinUI> inPins{
        {PinType::INPUT, 0, PinState::DISCONNECTED, {-2, 1}},
        {PinType::INPUT, 1, PinState::DISCONNECTED, {-2, -1}}
    };

    std::vector<PinUI> outPins{
        {PinType::OUTPUT, 0, PinState::DISCONNECTED, {2, 0}}
    };

    gatesViews.push_back(GateView({ 0, 0 }, gate0_id, { 0.2f, 0.2f }, "ANDgate", inPins, outPins));
    gatesViews.push_back(GateView({ 10, 0 }, gate1_id, { 0.2f, 0.2f }, "ANDgate", inPins, outPins));

    input.setGates(&gatesViews);

    Wire testWire1;
    testWire1.setPath({ {3, 0}, {5, 0}, {5, -3}, {8, -3} });
    testWire1.setState(PinState::ON);
    wires.push_back(testWire1);

    Wire testWire2;
    testWire2.setPath({ {0, -5}, {10, -5} });
    testWire2.setState(PinState::DISCONNECTED);
    wires.push_back(testWire2);

    input.setWires(&wires);

    // ==========================================
    // Main Loop
    // ==========================================
    while (!glfwWindowShouldClose(window))
    {
        // 1. Process Input and Logic
        input.process(window);

        circuit.getGate(gate0_id)->setStateInPins(0, true);
        circuit.getGate(gate0_id)->setStateInPins(1, true);

        try {
            circuit.propagate();
        }
        catch (const std::runtime_error& e) {
            std::cerr << "Simulation Error: " << e.what() << "\n";
        }

        for (const auto& event : input.consumeWireEvents()) {
            if (event.action == WireAction::CONNECT) {
                circuit.connectGates(event.srcGateId, event.destGateId, event.destPinIndex);
                std::cout << "Engine: Logic connected!\n";
            }
            else if (event.action == WireAction::DISCONNECT) {
                circuit.disconnectGates(event.srcGateId, event.destGateId, event.destPinIndex);
                std::cout << "Engine: Logic disconnected!\n";
            }
        }

        // ==========================================
        // 2. SYNC STATES (Logic -> UI)
        // ==========================================
        for (auto& gateView : gatesViews) {
            Gate* logicGate = circuit.getGate(gateView.getGateId());
            if (!logicGate) continue;

            bool outSignal = logicGate->getStateOutPin();
            gateView.getOutputPinUI().state = outSignal ? PinState::ON : PinState::OFF;

            auto inSignals = logicGate->getStateInPins();
            for (size_t i = 0; i < inSignals.size(); i++) {
                gateView.getInputPinUI(i).state = inSignals[i] ? PinState::ON : PinState::OFF;
            }
        }

        for (auto& wire : wires) {
            if (wire.hasSource()) {
                Gate* srcGate = circuit.getGate(wire.getSource().gateId);
                if (srcGate) {
                    bool outSignal = srcGate->getStateOutPin();
                    wire.setState(outSignal ? PinState::ON : PinState::OFF);
                }
            }
            else {
                wire.setState(PinState::DISCONNECTED);
            }
        }

        // ==========================================
                // 3. Camera & Rendering
                // ==========================================
        int width, height;
        glfwGetWindowSize(window, &width, &height);
        float aspectRatio = (height > 0) ? (static_cast<float>(width) / static_cast<float>(height)) : 1.0f;

        CameraState cam;
        cam.panOffset = input.getPanOffset();
        cam.zoom = input.getZoom();
        cam.aspectRatio = aspectRatio;
        cam.windowWidth = width;
        cam.windowHeight = height;

        m_renderer.beginFrame(cam);
        m_renderer.drawGrid();
        m_renderer.drawGates(gatesViews);

        // ==========================================
        // NEW: Draw Bounding Boxes (Selection Cues)
        // ==========================================

        // 1. Gate Bounding Box (Displays when clicked/selected AND when dragged)
        int selGate = input.getSelectedGateId();
        if (selGate != -1) {
            for (const auto& gv : gatesViews) {
                if (gv.getGateId() == selGate) {
                    m_renderer.drawGateBoundingBox(gv, 0.01f);
                    break;
                }
            }
        }

        // 2. Wire Bounding Box (Displays when clicked/selected)
        int selWire = input.getSelectedWireIndex();
        if (selWire != -1 && !input.isCurrentlyDrawingWire()) {
            if (selWire < wires.size()) {
                const Wire& targetWire = wires[selWire];

                // If it's part of a powered network, highlight the ENTIRE network!
                if (targetWire.hasSource()) {
                    for (const auto& w : wires) {
                        if (w.hasSource() &&
                            w.getSource().gateId == targetWire.getSource().gateId &&
                            w.getSource().pinIndex == targetWire.getSource().pinIndex) {
                            m_renderer.drawWireBoundingBox(w, 0.01f);
                        }
                    }
                }
                else {
                    // Just a floating wire, highlight it alone
                    m_renderer.drawWireBoundingBox(targetWire, 0.01f);
                }
            }
        }
        // ==========================================

        if (input.isCurrentlyDrawingWire()) {
            Wire active = input.getActiveWire();

            if (active.hasSource()) {
                Gate* srcGate = circuit.getGate(active.getSource().gateId);
                if (srcGate) {
                    bool outSignal = srcGate->getStateOutPin();
                    active.setState(outSignal ? PinState::ON : PinState::OFF);
                }
            }

            m_renderer.drawWires(wires, &active);
        }
        else {
            m_renderer.drawWires(wires, nullptr);
        }

        m_renderer.drawPins(gatesViews);

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    glfwTerminate();
}