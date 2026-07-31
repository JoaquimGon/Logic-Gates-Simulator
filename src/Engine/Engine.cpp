#include "..\..\include\Engine\Engine.h"

void Engine::processInput()
{
    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
        glfwSetWindowShouldClose(window, true);
}


int Engine::init(std::string windowName, int windowWidth, int windowHeight)
{
    // glfw: initialize and configure
    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    // glfw window creation
    window = glfwCreateWindow(windowWidth, windowHeight, windowName.c_str(), NULL, NULL);
    if (window == NULL)
    {
        std::cout << "Failed to create GLFW window" << std::endl;
        glfwTerminate();
        return -1;
    }
    glfwMakeContextCurrent(window);
    glfwSetFramebufferSizeCallback(window, Engine::resizeWindow);

    // glad: load all OpenGL function pointers
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
    {
        std::cout << "Failed to initialize GLAD" << std::endl;
        return -1;
    }

    
    sm.load("ANDgate", "shaders/quadShader.ver", "shaders/andGate.frag");
    

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
    gateLayout.addAttribute(3);
    gateLayout.applyToVAO();
    gateMesh = std::make_unique<Mesh>(quadVertices, quadIndices, gateLayout, GL_LINE_LOOP);

    return 0;

}

void Engine::run()
{
    // Main draw loop
    while (!glfwWindowShouldClose(window))
    {
        // input
        // -----
        processInput();

        // render
        // ------
        glClearColor(0.2f, 0.3f, 0.3f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);


        sm.get("ANDgate")->use();

        gateMesh->draw();

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    glfwTerminate();

}
