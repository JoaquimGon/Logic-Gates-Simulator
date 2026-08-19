#include "..\..\include\Engine\Engine.h"

Engine::Engine(std::string windowName, int windowWidth, int windowHeight)
{
    m_windowName = windowName;
    m_windowWidth = windowWidth;
    m_windowHeight = windowHeight;
}

int Engine::init()
{
    // glfw: initialize and configure
    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    // ==========================================
    // glfw Configuration
    // ==========================================
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
    glfwSetMouseButtonCallback(window, Input::mouseButtonCallback);
    glfwSetCursorPosCallback(window, Input::cursorPositionCallback);

    // ==========================================
    // glad Configuration
    // ==========================================
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
    {
        std::cout << "Failed to initialize GLAD" << std::endl;
        return -1;
    }

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glClearColor(0.05f, 0.05f, 0.05f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glEnable(GL_PROGRAM_POINT_SIZE);

    // ==========================================
    // Shaders
    // ==========================================
    sm.load("ANDgate", "shaders/andGate.vert", "shaders/andGate.frag");
    sm.load("grid", "shaders/vec4Shader.vert", "shaders/grid.frag");
    sm.load("pin", "shaders/pins/pins.vert", "shaders/pins/pins.frag");
    sm.load("wire", "shaders/wires/wires.vert", "shaders/wires/wires.frag");

    // ==========================================
    // Meshes
    // ==========================================
    // Gate Mesh creation
    std::vector<float> quadVertices = {
        -0.5f,  0.5f, 0.0, // Top-left
        -0.5f, -0.5f, 0.0, // Bottom-left
         0.5f, -0.5f, 0.0, // Bottom-right
         0.5f,  0.5f, 0.0 // Top-right
    };
    std::vector<unsigned int> quadIndices = {
        0, 1, 2,
        2, 3, 0
    };
    VertexLayout gateLayout;
    gateLayout.addAttribute(3);
    gateMesh = std::make_unique<Mesh>(quadVertices, quadIndices, gateLayout, GL_TRIANGLES);

    // Grid Mesh
    std::vector<float> fullScreenVertices = {
        -1.0f,  1.0f, -1.0f,
        -1.0f, -1.0f, -1.0f,
         1.0f, -1.0f, -1.0f,
         1.0f,  1.0f, -1.0f
    };
    std::vector<unsigned int> fullScreenIndeces = {
        0, 1, 2,
        2, 3, 0
    };
    VertexLayout gridLayout;
    gridLayout.addAttribute(3);
    gridMesh = std::make_unique<Mesh>(fullScreenVertices, fullScreenIndeces, gridLayout, GL_TRIANGLES);

    // Point mesh (Pins)
    std::vector<float> pointVertices = { 0.0f, 0.0f, 0.0f };
    VertexLayout pointLayout;
    pointLayout.addAttribute(3);
    pointMesh = std::make_unique<Mesh>(pointVertices, std::vector<unsigned int>{}, pointLayout, GL_POINTS);

    // Line mesh (Wires)
    std::vector<float> lineVertices = {
        0.0f, 0.0f, 0.0f,
        1.0f, 1.0f, 0.0f
    };
    VertexLayout lineLayout;
    lineLayout.addAttribute(3);
    // FIX: Using wireMesh and GL_LINES instead of overwriting pointMesh
    wireMesh = std::make_unique<Mesh>(lineVertices, std::vector<unsigned int>{}, lineLayout, GL_LINES);

    return 0;
}

void Engine::run()
{
    glm::mat4 cameraMatrix(1.0f);
    float cameraZoom{ 1.0f };
    glm::vec2 cameraPan(0.0f, 0.0f);

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

    while (!glfwWindowShouldClose(window))
    {
        // ==========================================
        //  Input & Camera
        // ==========================================
        input.process(window);

        cameraMatrix = glm::translate(glm::mat4(1.0f), glm::vec3(cameraPan, 0.0f));
        cameraMatrix = glm::scale(cameraMatrix, glm::vec3(cameraZoom, cameraZoom, 1.0f));

        int width, height;
        glfwGetWindowSize(window, &width, &height);
        float aspectRatio = (height > 0) ? (static_cast<float>(width) / static_cast<float>(height)) : 1.0f;

        // ==========================================
        // Draw Grid (Standard Draw)
        // ==========================================
        sm.get("grid")->use();
        sm.get("grid")->setVec2("uPanOffset", input.getPanOffset().x, input.getPanOffset().y);
        sm.get("grid")->setFloat("uZoom", cameraZoom);
        sm.get("grid")->setFloat("uGridSpacing", 0.05f);
        sm.get("grid")->setVec2("uResolution", float(width), float(height));
        gridMesh->draw();

        // ==========================================
        // Draw AND Gate Bodies (Instanced Draw)
        // ==========================================
        sm.get("ANDgate")->use();
        sm.get("ANDgate")->setVec2("uPanOffset", input.getPanOffset().x, input.getPanOffset().y);
        sm.get("ANDgate")->setFloat("uZoom", cameraZoom);
        sm.get("ANDgate")->setFloat("uAspectRatio", aspectRatio);

        // Use uniform for size since they are all the same size right now
        sm.get("ANDgate")->setVec2("uGateSize", 0.2f, 0.2f);

        // 1. Gather all gate positions
        std::vector<float> gatePositions;
        for (const auto& gateView : gatesViews) {
            gatePositions.push_back(gateView.getPosition().x);
            gatePositions.push_back(gateView.getPosition().y);
        }

        // 2. Upload instance data and draw all gates instantly
        if (!gatePositions.empty()) {
            gateMesh->setInstanceData(gatePositions, { 2 }, 1); // {2} means one attribute of vec2 at location 1
            gateMesh->drawInstanced(gatesViews.size());
        }

        // ==========================================
        // Draw Pin Points (Instanced Draw)
        // ==========================================
        auto* pinShader = sm.get("pin");
        pinShader->use();
        pinShader->setVec2("uPanOffset", input.getPanOffset().x, input.getPanOffset().y);
        pinShader->setFloat("uZoom", cameraZoom);
        pinShader->setFloat("uAspectRatio", aspectRatio);
        pinShader->setFloat("uPointSize", 10.0f);

        // 1. Gather all pin positions and colors into one interleaved array
        std::vector<float> pinInstanceData;
        int totalPins = 0;

        for (const auto& gateView : gatesViews) {
            for (const auto& pin : gateView.m_UIPins) {
                glm::vec2 pinWorldPos = gateView.getAbsolutePinWorldPos(pin);

                // Add Position (vec2)
                pinInstanceData.push_back(pinWorldPos.x);
                pinInstanceData.push_back(pinWorldPos.y);

                // Add Color (vec4)
                if (pin.state == PinState::DISCONNECTED) {
                    pinInstanceData.insert(pinInstanceData.end(), { 0.0f, 0.0f, 1.0f, 1.0f }); // Blue
                }
                else if (pin.state == PinState::ON) {
                    pinInstanceData.insert(pinInstanceData.end(), { 0.0f, 1.0f, 0.0f, 1.0f }); // Green
                }
                else {
                    pinInstanceData.insert(pinInstanceData.end(), { 1.0f, 0.0f, 0.0f, 1.0f }); // Red
                }
                totalPins++;
            }
        }

        // 2. Upload instance data (Positions and Colors) and draw all pins instantly
        if (totalPins > 0) {
            // {2, 4} tells the mesh we are sending a vec2 (Pos) followed by a vec4 (Color)
            pointMesh->setInstanceData(pinInstanceData, { 2, 4 }, 1);
            pointMesh->drawInstanced(totalPins);
        }

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    glfwTerminate();
}