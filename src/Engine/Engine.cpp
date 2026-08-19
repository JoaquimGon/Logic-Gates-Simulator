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
    float cameraZoom{ 1.0f };

    // ==========================================
    // Setup Test Data
    // ==========================================
    std::vector<GateView> gatesViews;
    std::vector<PinUI> standartGatePins
    {
        PinUI{0, PinState::DISCONNECTED, {-2, 1}},
        PinUI{1, PinState::DISCONNECTED, {-2, -1}},
        PinUI{2, PinState::DISCONNECTED, {2, 0}}
    };

    gatesViews.push_back(GateView({ 0, 0 }, { 0.2f, 0.2f }, "ANDgate", standartGatePins));
    gatesViews.push_back(GateView({ 10, 0 }, { 0.2f, 0.2f }, "ANDgate", standartGatePins));

    input.setGates(&gatesViews);

    std::vector<Wire> wires;
    Wire testWire1;
    testWire1.setPath({ {2, 0}, {5, 0}, {5, -3}, {8, -3} }); // A nice zig-zag
    testWire1.setState(PinState::ON); // Green
    wires.push_back(testWire1);

    Wire testWire2;
    testWire2.setPath({ {0, -5}, {10, -5} }); // A straight line below
    testWire2.setState(PinState::DISCONNECTED); // Blue
    wires.push_back(testWire2);

    // ==========================================
    // Main Loop
    // ==========================================
    while (!glfwWindowShouldClose(window))
    {
        // 1. Process Input and Logic
        input.process(window);

        // 2. Prepare Camera State
        int width, height;
        glfwGetWindowSize(window, &width, &height);
        float aspectRatio = (height > 0) ? (static_cast<float>(width) / static_cast<float>(height)) : 1.0f;

        CameraState cam;
        cam.panOffset = input.getPanOffset();
        cam.zoom = input.getZoom();;
        cam.aspectRatio = aspectRatio;
        cam.windowWidth = width;
        cam.windowHeight = height;

        // 3. Render Pipeline
        m_renderer.beginFrame(cam);
        m_renderer.drawGrid();
        m_renderer.drawGates(gatesViews);
        m_renderer.drawWires(wires);
        m_renderer.drawPins(gatesViews);

        // 4. Present Frame
        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    glfwTerminate();
}