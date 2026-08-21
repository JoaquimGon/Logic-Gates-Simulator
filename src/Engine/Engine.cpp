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
    Scene scene;

    std::vector<PinUI> inPins{
        {PinType::INPUT, 0, PinState::DISCONNECTED, {-2, 1}},
        {PinType::INPUT, 1, PinState::DISCONNECTED, {-2, -1}}
    };
    std::vector<PinUI> outPins{
        {PinType::OUTPUT, 0, PinState::DISCONNECTED, {2, 0}}
    };

    int gate0_id = scene.addGate(GateType::AND, { 0, 0 }, { 0.2f, 0.2f }, "ANDgate", inPins, outPins);
    int gate1_id = scene.addGate(GateType::AND, { 10, 0 }, { 0.2f, 0.2f }, "ANDgate", inPins, outPins);

    input.setScene(&scene);

    Wire testWire1;
    testWire1.setPath({ {3, 0}, {5, 0}, {5, -3}, {8, -3} });
    testWire1.setState(PinState::ON);
    scene.commitWire(testWire1);

    Wire testWire2;
    testWire2.setPath({ {0, -5}, {10, -5} });
    testWire2.setState(PinState::DISCONNECTED);
    scene.commitWire(testWire2);

    while (!glfwWindowShouldClose(window))
    {
        input.process(window);

        scene.getLogicGate(gate0_id)->setStateInPins(0, true);
        scene.getLogicGate(gate0_id)->setStateInPins(1, true);

        try { scene.propagate(); }
        catch (const std::runtime_error& e) { std::cerr << "Simulation Error: " << e.what() << "\n"; }

        scene.syncVisuals();

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
        m_renderer.drawGates(scene.getGateViewMap());

        int selGate = input.getSelectedGateId();
        if (selGate != -1) {
            if (GateView* gv = scene.getGateView(selGate)) {
                m_renderer.drawGateBoundingBox(*gv, 0.01f);
            }
        }

        if (!input.isCurrentlyDrawingWire() && input.hasSelectedSegment()) {
            m_renderer.drawWireSegmentBoundingBox(input.getSelectedSegmentStart(), input.getSelectedSegmentEnd(), 0.01f);
        }

        if (input.isCurrentlyDrawingWire()) {
            Wire active = input.getActiveWire();
            if (active.hasSource()) {
                if (Gate* srcGate = scene.getLogicGate(active.getSource().gateId))
                    active.setState(srcGate->getStateOutPin() ? PinState::ON : PinState::OFF);
            }
            m_renderer.drawWires(scene.getWires(), &active);
        }
        else {
            m_renderer.drawWires(scene.getWires(), nullptr);
        }

        m_renderer.drawPins(scene.getGateViewMap());

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    glfwTerminate();
}