#include "Engine.h"

#include "Components/Definitions/NativeDefinitions.h"
#include "Editor/Actions/EditorActions.h"
#include "Editor/Scene.h"
#include "Graphics/Presentation/ComponentPresentation.h"

#include <GLFW/glfw3.h>
#include <glad/glad.h>
#include <iostream>
#include <vector>

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

    glfwSetWindowUserPointer(window, &input);

    // Mouse and keyboard callbacks. Keys are handled as events rather than
    // polled for consistent and responsive behaviour
    glfwSetMouseButtonCallback(window, Input::mouseButtonCallback);
    glfwSetCursorPosCallback(window, Input::cursorPositionCallback);
    glfwSetScrollCallback(window, Input::scrollCallback);
    glfwSetKeyCallback(window, Input::keyCallback);
    glfwSetCharCallback(window, Input::charCallback);
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
    if (!m_renderer.init())
    {
        shutdown();
        return -1;
    }

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
    auto updateUiLayout = [&]()
    { m_ui.layout(scene.getComponentCatalog(), input.getCameraFrame(window).surface, input); };
    updateUiLayout();
    input.setUiInputHandler(
        [&](const UiInputEvent& event)
        {
            updateUiLayout();
            return m_ui.handleInput(event, scene, input, input.getCameraFrame(window));
        }
    );

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
        updateUiLayout();
        input.process(window);
        if (displayedMode != input.getMode())
        {
            displayedMode = input.getMode();
            glfwSetWindowTitle(window, titleForMode().c_str());
        }

        // Clocks settle the circuit at every transition; remember edits for visual synchronization.
        const bool pendingSimulation = scene.isSimulationDirty();
        bool clockEdgeFlipped = scene.updateClocks(deltaTime);

        // Debugging
        m_timeSinceLastPropagateMs += (deltaTime * 1000.0f);


        if (clockEdgeFlipped || pendingSimulation || scene.isSimulationDirty())
        {
            SimulationResult simulationResult =
                scene.isSimulationDirty() ? scene.propagate() : scene.getLastEvalResult();

            // Debugging
            m_timeSinceLastPropagateMs = 0.0f;


            if (simulationResult != m_lastSimulationResult)
            {
                m_lastSimulationResult = simulationResult;

                if (simulationResult == SimulationResult::NON_CONVERGENT)
                {
                    std::cerr << "[Simulation] Signals did not settle within the safety limit; "
                                 "simulation is paused. Change an input or repair the circuit.\n";
                }
                else if (simulationResult == SimulationResult::CONNECTION_REJECTED)
                {
                    std::cerr << "[Simulation] Connection rejected; "
                                 "simulation is paused until the wiring is repaired.\n";
                }
                else
                {
                    std::cerr << "[Simulation] Signals settled, simulation resumed.\n";
                }
            }
            scene.syncVisuals();
        }

        // 4. Render graphics as normal
        m_renderer.beginFrame(input.getCameraFrame(window));
        const auto components = buildComponentPresentation(scene.getComponentViewMap());
        const auto junctions = scene.getWireIntersections();
        CanvasFrame frame{components, scene.getWires(), junctions};
        const int selected = input.getSelectedComponentId();
        const int highlighted = selected != -1 ? selected : input.getHoveredComponentId();
        if (const auto* view = scene.getComponentView(highlighted))
            frame.bodyHighlight =
                BodyHighlight{view->getPosition(), view->getSize(), selected != -1 ? 1.0f : 0.4f};
        if (!input.isCurrentlyDrawingWire())
        {
            if (input.hasSelectedSegment())
                frame.segmentHighlight = SegmentHighlight{
                    input.getSelectedSegmentStart(), input.getSelectedSegmentEnd(), 1
                };
            else if (input.hasHoveredSegment())
                frame.segmentHighlight = SegmentHighlight{
                    input.getHoveredSegmentStart(), input.getHoveredSegmentEnd(), 0.4f
                };
        }
        std::optional<Wire> active;
        if (input.isCurrentlyDrawingWire())
        {
            active = input.getActiveWire();
            if (input.getWireOriginPin().isConnected())
                active->setState(
                    scene.pinState(input.getWireOriginPin(), input.getWireOriginType())
                );
            frame.activeWire = &*active;
            frame.gridHighlight = GridHighlight{input.getCurrentGridCoords(), 1};
        }
        else if (
            input.isCanvasPointerAvailable(window) && input.isIdle() &&
            input.getHoveredComponentId() == -1 && input.getHoveredPinComponentId() == -1
        )
            frame.gridHighlight = GridHighlight{input.getCurrentGridCoords(), 0.4f};
        frame.hoveredComponent = input.getHoveredPinComponentId();
        frame.hoveredPin = input.getHoveredPinIndex();
        frame.hoveredDirection = input.getHoveredPinType();
        m_renderer.drawCanvas(frame);

        // Debugging
        if (m_showDebugOverlay || scene.getLastEvalResult() != SimulationResult::OK)
        {
            DebugMetrics metrics;
            metrics.fps = m_fps;
            metrics.frameTimeMs = (deltaTime > 0.0f) ? (deltaTime * 1000.0f) : 0.0f;
            metrics.drawCalls = m_renderer.getDrawCallCount();
            metrics.lastPropagateMs = scene.getLastPropagateTimeMs();
            metrics.timeSinceLastPropagateMs = m_timeSinceLastPropagateMs;
            metrics.scheduledComponentCount = scene.getSimulationComponentCount();
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

        m_ui.draw(m_renderer, scene, input.getCameraFrame(window));
        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    m_ui.cancel(input);
    input.setUiInputHandler({});
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
