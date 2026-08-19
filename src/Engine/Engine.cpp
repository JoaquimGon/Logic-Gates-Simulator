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

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
    {
        std::cout << "Failed to initialize GLAD" << std::endl;
        return -1;
    }

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glClearColor(0.05f, 0.05f, 0.05f, 1.0f);
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
    // Gate Mesh
    std::vector<float> quadVertices = {
        -0.5f,  0.5f, 0.0,
        -0.5f, -0.5f, 0.0,
         0.5f, -0.5f, 0.0,
         0.5f,  0.5f, 0.0
    };
    std::vector<unsigned int> quadIndices = { 0, 1, 2, 2, 3, 0 };
    VertexLayout gateLayout;
    gateLayout.addAttribute(3);
    gateMesh = std::make_unique<Mesh>(quadVertices, quadIndices, gateLayout, GL_TRIANGLES);

    // Grid Mesh
    std::vector<float> fullScreenVertices = {
        -1.0f,  1.0f, -1.0f, -1.0f, -1.0f, -1.0f,
         1.0f, -1.0f, -1.0f,  1.0f,  1.0f, -1.0f
    };
    std::vector<unsigned int> fullScreenIndeces = { 0, 1, 2, 2, 3, 0 };
    VertexLayout gridLayout;
    gridLayout.addAttribute(3);
    gridMesh = std::make_unique<Mesh>(fullScreenVertices, fullScreenIndeces, gridLayout, GL_TRIANGLES);

    // Pin Mesh
    std::vector<float> pointVertices = { 0.0f, 0.0f, 0.0f };
    VertexLayout pointLayout;
    pointLayout.addAttribute(3);
    pointMesh = std::make_unique<Mesh>(pointVertices, std::vector<unsigned int>{}, pointLayout, GL_POINTS);

    // Wire Mesh 
    VertexLayout wireLayout;
    wireLayout.addAttribute(3); // Location 0: Position (X, Y, Z)
    wireLayout.addAttribute(4); // Location 1: Color (R, G, B, A)
    wireMesh = std::make_unique<Mesh>(std::vector<float>{}, std::vector<unsigned int>{}, wireLayout, GL_LINES);

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

    // ==========================================
    // Setup Test Wires
    // ==========================================
    std::vector<Wire> wires;

    Wire testWire1;
    testWire1.setPath({ {2, 0}, {5, 0}, {5, -3}, {8, -3} }); // A nice zig-zag
    testWire1.setState(PinState::ON); // Green
    wires.push_back(testWire1);

    Wire testWire2;
    testWire2.setPath({ {0, -5}, {10, -5} }); // A straight line below
    testWire2.setState(PinState::DISCONNECTED); // Blue
    wires.push_back(testWire2);


    while (!glfwWindowShouldClose(window))
    {
        input.process(window);
        glClear(GL_COLOR_BUFFER_BIT); // Make sure to clear the screen!

        int width, height;
        glfwGetWindowSize(window, &width, &height);
        float aspectRatio = (height > 0) ? (static_cast<float>(width) / static_cast<float>(height)) : 1.0f;

        // ==========================================
        // 1. Draw Grid
        // ==========================================
        sm.get("grid")->use();
        sm.get("grid")->setVec2("uPanOffset", input.getPanOffset().x, input.getPanOffset().y);
        sm.get("grid")->setFloat("uZoom", cameraZoom);
        sm.get("grid")->setFloat("uGridSpacing", 0.05f);
        sm.get("grid")->setVec2("uResolution", float(width), float(height));
        gridMesh->draw();

        // ==========================================
        // 2. Draw AND Gate Bodies
        // ==========================================
        sm.get("ANDgate")->use();
        sm.get("ANDgate")->setVec2("uPanOffset", input.getPanOffset().x, input.getPanOffset().y);
        sm.get("ANDgate")->setFloat("uZoom", cameraZoom);
        sm.get("ANDgate")->setFloat("uAspectRatio", aspectRatio);
        sm.get("ANDgate")->setVec2("uGateSize", 0.2f, 0.2f);

        std::vector<float> gatePositions;
        for (const auto& gateView : gatesViews) {
            gatePositions.push_back(gateView.getPosition().x);
            gatePositions.push_back(gateView.getPosition().y);
        }

        if (!gatePositions.empty()) {
            gateMesh->setInstanceData(gatePositions, { 2 }, 1);
            gateMesh->drawInstanced(gatesViews.size());
        }

        // ==========================================
        // 3. Draw Wires (Dynamic Batching)
        // ==========================================
        auto* wireShader = sm.get("wire");
        wireShader->use();
        wireShader->setVec2("uPanOffset", input.getPanOffset().x, input.getPanOffset().y);
        wireShader->setFloat("uZoom", cameraZoom);
        wireShader->setFloat("uAspectRatio", aspectRatio);

        std::vector<float> allWiresData;

        // Gather the geometry for every wire on the board
        for (const auto& wire : wires) {
            std::vector<float> singleWireData = wire.getBatchedVertexData();
            // Append it to our master list
            allWiresData.insert(allWiresData.end(), singleWireData.begin(), singleWireData.end());
        }

        if (!allWiresData.empty()) {
            // 7 floats per vertex: (X, Y, Z,  R, G, B, A)
            wireMesh->updateData(allWiresData, 7);
            wireMesh->draw();
        }

        // ==========================================
        // 4. Draw Pin Points
        // ==========================================
        auto* pinShader = sm.get("pin");
        pinShader->use();
        pinShader->setVec2("uPanOffset", input.getPanOffset().x, input.getPanOffset().y);
        pinShader->setFloat("uZoom", cameraZoom);
        pinShader->setFloat("uAspectRatio", aspectRatio);
        pinShader->setFloat("uPointSize", 10.0f);

        std::vector<float> pinInstanceData;
        int totalPins = 0;

        for (const auto& gateView : gatesViews) {
            for (const auto& pin : gateView.m_UIPins) {
                glm::vec2 pinWorldPos = gateView.getAbsolutePinWorldPos(pin);
                pinInstanceData.push_back(pinWorldPos.x);
                pinInstanceData.push_back(pinWorldPos.y);

                if (pin.state == PinState::DISCONNECTED) {
                    pinInstanceData.insert(pinInstanceData.end(), { 0.0f, 0.0f, 1.0f, 1.0f });
                }
                else if (pin.state == PinState::ON) {
                    pinInstanceData.insert(pinInstanceData.end(), { 0.0f, 1.0f, 0.0f, 1.0f });
                }
                else {
                    pinInstanceData.insert(pinInstanceData.end(), { 1.0f, 0.0f, 0.0f, 1.0f });
                }
                totalPins++;
            }
        }

        if (totalPins > 0) {
            pointMesh->setInstanceData(pinInstanceData, { 2, 4 }, 1);
            pointMesh->drawInstanced(totalPins);
        }

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    glfwTerminate();
}