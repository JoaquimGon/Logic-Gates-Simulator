#include "Engine.h"

#include "Components/Definitions/NativeDefinitions.h"
#include "Editor/Actions/EditorActions.h"
#include "Editor/Scene.h"

#include <GLFW/glfw3.h>
#include <glad/glad.h>
#include <iostream>
#include <vector>

void Engine::resizeWindow(GLFWwindow* window, int width, int height)
{
    glViewport(0, 0, width, height);
}

void Engine::errorCallback(int error, const char* description)
{
    std::cerr << "[GLFW] Error " << error << ": " << description << std::endl;
}

Engine::Engine(std::string windowName, int windowWidth, int windowHeight)
{
    m_windowName = windowName;
    m_windowWidth = windowWidth;
    m_windowHeight = windowHeight;
}


Engine::~Engine()
{
    // Safety net for the paths that never reach the end of run(): a failed
    // init(), or run() returning early. shutdown() is idempotent, so this is a
    // no-op when run() already tore everything down.
    shutdown();
}


int Engine::init()
{
    // ==========================================
    // glfw Configuration
    // ==========================================
    // Registered before glfwInit() so failures raised during initialization
    // itself (an unsupported context version, for instance) are reported too.
    glfwSetErrorCallback(Engine::errorCallback);

    if (!glfwInit())
    {
        std::cerr << "Failed to initialize GLFW" << std::endl;
        return -1;
    }
    m_glfwInitialized = true;

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    window = glfwCreateWindow(m_windowWidth, m_windowHeight, m_windowName.c_str(), NULL, NULL);
    if (window == NULL)
    {
        std::cerr << "Failed to create GLFW window" << std::endl;
        shutdown(); // nothing to delete yet, but the window and GLFW must not
                    // leak
        return -1;
    }

    glfwMakeContextCurrent(window);

    // Cap the frame rate at the display's refresh rate. Without this the loop
    // runs unthrottled, which pegs the GPU (and a CPU core) even on a static
    // scene, and it would make any future time-based simulation frame-rate
    // dependent.
    glfwSwapInterval(1);

    glfwSetFramebufferSizeCallback(window, Engine::resizeWindow);
    glfwSetWindowUserPointer(window, &input);

    // Mouse and keyboard callbacks. Keys are handled as events rather than
    // polled for consistent and responsive behaviour
    glfwSetMouseButtonCallback(window, Input::mouseButtonCallback);
    glfwSetCursorPosCallback(window, Input::cursorPositionCallback);
    glfwSetScrollCallback(window, Input::scrollCallback);
    glfwSetKeyCallback(window, Input::keyCallback);
    glfwSetWindowFocusCallback(window, Input::focusCallback);

    // ==========================================
    // glad Configuration
    // ==========================================
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
    {
        std::cerr << "Failed to initialize GLAD" << std::endl;
        shutdown(); // no GL objects exist yet; releases the window and GLFW
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
    // The loop below needs the window and GL context created by init(). Without
    // them every GLFW call in here would be running on a null window.
    if (!window)
    {
        std::cerr << "[Engine] run() called without a successful init(); aborting.\n";
        return;
    }

    Scene scene;

    const auto initialScene = EditorActions(scene).apply(
        {CreateComponent{BuiltinComponentIds::And, {0, 0}, {}},
         CreateComponent{BuiltinComponentIds::Nand, {0, 10}, {}},
         CreateComponent{BuiltinComponentIds::Or, {10, 0}, {}},
         CreateComponent{BuiltinComponentIds::Nor, {10, 10}, {}},
         CreateComponent{BuiltinComponentIds::Xor, {-10, 0}, {}},
         CreateComponent{BuiltinComponentIds::Nxor, {-10, 10}, {}}}
    );
    if (!initialScene)
    {
        std::cerr << "[Editor] Initial scene could not be created: " << initialScene.message
                  << '\n';
        return;
    }

    input.setScene(&scene);

    auto titleForMode = [&]()
    { return m_windowName + " - " + editorModeName(input.getMode()) + " mode (F2 to switch)"; };
    glfwSetWindowTitle(window, titleForMode().c_str());
    auto displayedMode = input.getMode();
    double lastFrameTime = glfwGetTime();

    while (!glfwWindowShouldClose(window))
    {
        double currentFrameTime = glfwGetTime();
        float deltaTime = static_cast<float>(currentFrameTime - lastFrameTime);
        lastFrameTime = currentFrameTime;

        // Calculate rolling FPS
        m_frameCount++;
        m_frameTimeAccumulator += deltaTime;
        if (m_frameTimeAccumulator >= 0.25f)
        {
            m_fps = static_cast<float>(m_frameCount) / m_frameTimeAccumulator;
            m_frameCount = 0;
            m_frameTimeAccumulator = 0.0f;
        }

        // Toggle Debug HUD with F3
        if (input.consumeKeyPress(GLFW_KEY_F3))
        {
            m_showDebugOverlay = !m_showDebugOverlay;
        }

        // 1. Process OS user inputs
        input.process(window);
        if (displayedMode != input.getMode())
        {
            displayedMode = input.getMode();
            glfwSetWindowTitle(window, titleForMode().c_str());
        }

        // 2. Advance time for any clocks in the circuit
        bool clockEdgeFlipped = scene.updateClocks(deltaTime);

        // Debugging
        m_timeSinceLastPropagateMs += (deltaTime * 1000.0f);


        // 3. EVENT-DRIVEN SIMULATION:
        // Only run DFS/propagation when an edge flipped or a component/wire was
        // touched
        if (clockEdgeFlipped || scene.isSimulationDirty())
        {
            EvalOrderResult orderResult = scene.propagate();

            // Debugging
            m_timeSinceLastPropagateMs = 0.0f;


            if (orderResult != m_lastOrderResult)
            {
                m_lastOrderResult = orderResult;

                if (orderResult == EvalOrderResult::CYCLE_DETECTED)
                {
                    std::cerr << "[Simulation] Feedback connection rejected; "
                                 "simulation is paused until the wiring is repaired.\n";
                }
                else if (orderResult == EvalOrderResult::CONNECTION_REJECTED)
                {
                    std::cerr << "[Simulation] Connection rejected; "
                                 "simulation is paused until the wiring is repaired.\n";
                }
                else
                {
                    std::cerr << "[Simulation] Evaluation order rebuilt, "
                                 "simulation resumed.\n";
                }
            }
            scene.syncVisuals();
        }

        // 4. Render graphics as normal
        int width, height;
        glfwGetWindowSize(window, &width, &height);
        float aspectRatio =
            (height > 0) ? (static_cast<float>(width) / static_cast<float>(height)) : 1.0f;

        CameraState cam;
        cam.panOffset = input.getPanOffset();
        cam.zoom = input.getZoom();
        cam.aspectRatio = aspectRatio;
        cam.windowWidth = width;
        cam.windowHeight = height;

        m_renderer.beginFrame(cam);
        m_renderer.drawGrid();
        m_renderer.drawComponents(scene.getComponentViewMap()); // was drawGates

        // ==========================================
        // Highlight Component
        // ==========================================
        if (input.getSelectedComponentId() != -1)
        {
            // Selected: 100% Opacity
            if (ComponentView* cv = scene.getComponentView(input.getSelectedComponentId()))
            {
                m_renderer.drawComponentBoundingBox(*cv, 0.01f, 1.0f);
            }
        }
        else if (input.getHoveredComponentId() != -1)
        {
            // Hovered: 40% Opacity
            if (ComponentView* cv = scene.getComponentView(input.getHoveredComponentId()))
            {
                m_renderer.drawComponentBoundingBox(*cv, 0.01f, 0.4f);
            }
        }

        // ==========================================
        // Highlight Wire Segment
        // ==========================================
        if (!input.isCurrentlyDrawingWire())
        {
            if (input.hasSelectedSegment())
            {
                // Selected: 100% Opacity
                m_renderer.drawWireSegmentBoundingBox(
                    input.getSelectedSegmentStart(), input.getSelectedSegmentEnd(), 0.01f, 1.0f
                );
            }
            else if (input.hasHoveredSegment())
            {
                // Hovered: 40% Opacity
                m_renderer.drawWireSegmentBoundingBox(
                    input.getHoveredSegmentStart(), input.getHoveredSegmentEnd(), 0.01f, 0.4f
                );
            }
        }

        if (input.isCurrentlyDrawingWire())
        {
            Wire active = input.getActiveWire();
            if (input.getWireOriginPin().isConnected())
            {
                active.setState(
                    scene.pinState(input.getWireOriginPin(), input.getWireOriginType())
                );
            }
            m_renderer.drawWires(scene.getWires(), &active);
        }
        else
        {
            m_renderer.drawWires(scene.getWires(), nullptr);
        }

        m_renderer.drawIntersections(scene.getWireIntersections());

        // Point highligh (for wire creation and mouse position)
        bool overEmptyOrWire =
            (input.getHoveredComponentId() == -1 && input.getHoveredPinComponentId() == -1);
        if (input.isCurrentlyDrawingWire())
        {
            m_renderer.drawGridPointHighlight(input.getCurrentGridCoords(), 1.0f);
        }
        else if (input.isIdle() && overEmptyOrWire)
        {
            m_renderer.drawGridPointHighlight(input.getCurrentGridCoords(), 0.4f);
        }

        m_renderer.drawPins(
            scene.getComponentViewMap(),
            input.getHoveredPinComponentId(),
            input.getHoveredPinIndex(),
            input.getHoveredPinType()
        );

        m_renderer.drawLabels(scene.getComponentViewMap());

        // Debugging
        if (m_showDebugOverlay || scene.getLastEvalResult() != EvalOrderResult::OK)
        {
            DebugMetrics metrics;
            metrics.fps = m_fps;
            metrics.frameTimeMs = (deltaTime > 0.0f) ? (deltaTime * 1000.0f) : 0.0f;
            metrics.drawCalls = m_renderer.getDrawCallCount();
            metrics.lastPropagateMs = scene.getLastPropagateTimeMs();
            metrics.timeSinceLastPropagateMs = m_timeSinceLastPropagateMs;
            metrics.evalOrderCount = scene.getEvalOrderSize();
            metrics.totalComponents = scene.getComponentCount();
            metrics.evalResult = scene.getLastEvalResult();

            // Topology metrics
            metrics.netCount = scene.netCount();
            metrics.wireCount = scene.wireCount();
            metrics.shortedNetCount = scene.getShortedNetCount();
            metrics.rejectedConnectionCount = scene.getRejectedConnections().size();

            // Interaction & Cursor metrics
            metrics.hoveredCompId = input.getHoveredComponentId();
            metrics.hoveredPinComponentId = input.getHoveredPinComponentId();
            metrics.hoveredPinIdx = input.getHoveredPinIndex();
            metrics.hoveredPinType = input.getHoveredPinType();
            metrics.hoveredWireId = input.getHoveredWireId();
            metrics.selectedCompId = input.getSelectedComponentId();
            metrics.cursorGrid = input.getCurrentGridCoords();

            m_renderer.drawDebugOverlay(metrics, m_showDebugOverlay);
        }

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    input.setScene(nullptr);
    shutdown();
}


void Engine::shutdown()
{
    // 1. Delete the GL objects while the window's context is still current:
    //    programs, VAOs and VBOs must not outlive the context they were created
    //    in.
    m_renderer.shutdown();

    // 2. Destroy the window explicitly. glfwTerminate() would do this as well,
    // but
    //    doing it here keeps the teardown order unambiguous.
    if (window)
    {
        glfwDestroyWindow(window);
        window = nullptr;
    }

    // 3. Shut GLFW down once, and only if it was actually initialized.
    if (m_glfwInitialized)
    {
        glfwTerminate();
        m_glfwInitialized = false;
    }
}