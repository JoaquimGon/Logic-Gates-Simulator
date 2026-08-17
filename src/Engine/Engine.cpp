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
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);



    // glfw window creation
    window = glfwCreateWindow(m_windowWidth, m_windowHeight, m_windowName.c_str(), NULL, NULL);
    if (window == NULL)
    {
        std::cout << "Failed to create GLFW window" << std::endl;
        glfwTerminate();
        return -1;
    }


    // ----- glfw configuration -----
    glfwMakeContextCurrent(window);
    glfwSetFramebufferSizeCallback(window, Engine::resizeWindow);
    // mouse callbacks
    glfwSetWindowUserPointer(window, &input);
    glfwSetMouseButtonCallback(window, Input::mouseButtonCallback);
    glfwSetCursorPosCallback(window, Input::cursorPositionCallback);

    // glad: load all OpenGL function pointers
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
    {
        std::cout << "Failed to initialize GLAD" << std::endl;
        return -1;
    }

    // ----- openGL configurations -----
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glClearColor(0.05f, 0.05f, 0.05f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    // ----- Shaders -----
    sm.load("ANDgate", "shaders/andGate.vert", "shaders/andGate.frag");
    sm.load("grid", "shaders/vec4Shader.vert", "shaders/grid.frag");
    

    // ----- Meshes -----
    // Gate Mesh creation
    std::vector<float> quadVertices = {
        -0.5f,  0.5f, 0.0, // Top-left
        -0.5f, -0.5f, 0.0, // Bottom-left
         0.5f, -0.5f, 0.0, // Bottom-right
         0.5f,  0.5f, 0.0 // Top-right
    };
    std::vector<unsigned int> quadIndices = {
        0, 1, 2,  // First triangle
        2, 3, 0   // Second triangle
    };
    VertexLayout gateLayout;
    gateLayout.addAttribute(3); // Vertex Position
    gateLayout.applyToVAO();
    gateMesh = std::make_unique<Mesh>(quadVertices, quadIndices, gateLayout, GL_TRIANGLES);


    // Grid Mesh creation to stay in the back the Z is put at -1
    std::vector<float> fullScreenVertices = {
        -1.0f,  1.0f, -1.0f, // Top-left
        -1.0f, -1.0f, -1.0f, // Bottom-left
         1.0f, -1.0f, -1.0f, // Bottom-right
         1.0f,  1.0f, -1.0f // Top-right
    };
    std::vector<unsigned int> fullScreenIndeces = {
        0, 1, 2,  // First triangle
        2, 3, 0   // Second triangle
    };
    VertexLayout gridLayout;
    gridLayout.addAttribute(3); // Vertex Position
    gridLayout.applyToVAO();
    gridMesh = std::make_unique<Mesh>(fullScreenVertices, fullScreenIndeces, gridLayout, GL_TRIANGLES);

    return 0;

}

void Engine::run()
{
    // Standart values
    glm::mat4 cameraMatrix(1.0f);
    float cameraZoom{ 1.0f };
    glm::vec2 cameraPan(0.0f, 0.0f);


    // Start the gate array
    std::vector<GateView> gatesViews;
    gatesViews.push_back(GateView({ 0.0f, 0.0f }, { 0.2f, 0.2f }, "ANDgate"));
    gatesViews.push_back(GateView({ 1.0f, 0.0f }, { 0.2f, 0.2f }, "ANDgate"));


    // Main draw loop
    while (!glfwWindowShouldClose(window))
    {
        // input
        // -----
        input.process(window);

        // render
        // ------

        // Camera position and zoom
        cameraMatrix = glm::translate(cameraMatrix, glm::vec3(cameraPan, 0.0f));
        cameraMatrix = glm::scale(cameraMatrix, glm::vec3(cameraZoom, cameraZoom, 1.0f));

        int width, height;
        glfwGetWindowSize(window, &width, &height);
        // Guard against division by zero if minimized
        float aspectRatio = (height > 0) ? (static_cast<float>(width) / static_cast<float>(height)) : 1.0f;

        // Draw grid
        sm.get("grid")->use();
        sm.get("grid")->setVec2("uPanOffset", input.getPanOffset().x, input.getPanOffset().y);
        sm.get("grid")->setFloat("uZoom", cameraZoom);
        sm.get("grid")->setFloat("uGridSpacing", 0.05f);
        sm.get("grid")->setVec2("uResolution", float(width), float(height));
        gridMesh->draw();

        // AND gates
        sm.get("ANDgate")->use();
        sm.get("ANDgate")->setVec2("uPanOffset", input.getPanOffset().x, input.getPanOffset().y);
        sm.get("ANDgate")->setFloat("uZoom", cameraZoom);
        sm.get("ANDgate")->setFloat("uAspectRatio", aspectRatio); // <-- was missing

        for (const auto& gateView : gatesViews)
        {
            sm.get(gateView.getShaderName())->setVec2("uGatePosition", gateView.getPosition().x, gateView.getPosition().y);
            sm.get(gateView.getShaderName())->setVec2("uGateSize", gateView.getSize().x, gateView.getSize().y);
            gateMesh->draw();
        }



        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    glfwTerminate();

}
