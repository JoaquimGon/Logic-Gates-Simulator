#include "Engine.h"

#include "App/CircuitFiles.h"
#include "App/UnsavedWork.h"
#include "Components/Definitions/NativeDefinitions.h"
#include "Editor/Actions/EditorActions.h"
#include "Editor/CircuitViews.h"
#include "Editor/Scene.h"
#include "Graphics/Presentation/ComponentPresentation.h"
#include "Persistence/CircuitFile.h"
#include "Persistence/SubcircuitLibrary.h"

#include <GLFW/glfw3.h>
#include <chrono>
#include <glad/glad.h>
#include <iostream>
#include <optional>
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

    CircuitViews circuits;

    const auto initialScene = EditorActions(circuits.activeScene())
                                  .apply(
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

    circuits.select(0, input);
    m_ui.setCircuitViews(&circuits, &m_renderer.fontMetrics());
    std::unique_ptr<SubcircuitLibrary> library;
    try
    {
        library = std::make_unique<SubcircuitLibrary>();
        refreshSubcircuitLibrary(*library, circuits, m_ui);
        for (const auto& error : library->errors())
            m_ui.addMessage("Library: " + error);
    }
    catch (const std::exception& error)
    {
        m_ui.setFileStatus(std::string("Library unavailable: ") + error.what(), true);
    }
    auto updateUiLayout = [&]()
    {
        const bool wasInspecting = circuits.inspecting();
        if (wasInspecting && !circuits.refreshInspection(input, false))
            m_ui.dismissPopups(input);
        m_ui.layout(
            circuits.displayedScene().getComponentCatalog(),
            input.getCameraFrame(window).surface,
            input
        );
    };
    updateUiLayout();
    input.setUiInputHandler(
        [&](const UiInputEvent& event)
        {
            updateUiLayout();
            m_ui.update(glfwGetTime(), input);
            const bool consumed = m_ui.handleInput(
                event, circuits.displayedScene(), input, input.getCameraFrame(window)
            );
            updateUiLayout();
            return consumed;
        }
    );

    input.setEditErrorHandler([&](const std::string& message) { m_ui.addMessage(message); });

    auto titleForMode = [&]()
    {
        return input.isReadOnly() ? m_windowName + " - Live subcircuit view (read only)"
                                  : m_windowName + " - " + editorModeName(input.getMode()) +
                                        " mode (F2 to switch)";
    };
    glfwSetWindowTitle(window, titleForMode().c_str());
    auto displayedMode = input.getMode();
    bool displayedReadOnly = input.isReadOnly();
    double lastFrameTime = glfwGetTime();
    std::optional<std::pair<std::size_t, UI::BottomTab>> timingContext;

    UnsavedWork unsaved;
    auto runFileCommand = [&](UI::FileCommand command)
    { return performFileCommand(command, window, m_ui, circuits, input, library.get()); };
    while (true)
    {
        if (glfwWindowShouldClose(window))
        {
            glfwSetWindowShouldClose(window, GLFW_FALSE);
            unsaved.request(
                {UnsavedWork::Operation::Exit, circuits.activeIndex()}, circuits, m_ui, input
            );
        }
        if (const auto index = m_ui.takeCloseCircuit())
            unsaved.request({UnsavedWork::Operation::Close, *index}, circuits, m_ui, input);
        if (const auto example = m_ui.takeExample())
        {
            try
            {
                openExampleCircuit(
                    *example, m_ui, circuits, input, input.getCameraFrame(window).surface
                );
            }
            catch (const std::exception& error)
            {
                m_ui.setFileStatus(error.what(), true);
            }
            lastFrameTime = glfwGetTime();
        }
        if (const auto command = m_ui.takeFileCommand())
        {
            if (*command == UI::FileCommand::Open)
                unsaved.request(
                    {UnsavedWork::Operation::Open, circuits.activeIndex()}, circuits, m_ui, input
                );
            else
            {
                try
                {
                    runFileCommand(*command);
                }
                catch (const std::exception& error)
                {
                    m_ui.setFileStatus(error.what(), true);
                    std::cerr << "[File] " << error.what() << '\n';
                }
                input.setUiCapture({});
                input.setCanvasFocused(true);
            }
            // Native dialogs are not simulated clock time.
            lastFrameTime = glfwGetTime();
        }
        const bool wasPending = unsaved.pending();
        if (const auto approved = unsaved.resolve(
                circuits, m_ui, input, [&]() { return runFileCommand(UI::FileCommand::Save); }
            ))
        {
            if (approved->operation == UnsavedWork::Operation::Exit)
                break;
            if (approved->operation == UnsavedWork::Operation::Close)
                circuits.remove(approved->index, input);
            else
            {
                try
                {
                    runFileCommand(UI::FileCommand::Open);
                }
                catch (const std::exception& error)
                {
                    m_ui.setFileStatus(error.what(), true);
                }
            }
            lastFrameTime = glfwGetTime();
        }
        if (wasPending && !unsaved.pending())
            lastFrameTime = glfwGetTime();
        if (const auto path = m_ui.takeOpenDraft())
        {
            try
            {
                auto saved = loadSubcircuit(*path);
                m_ui.dismissPopups(input);
                input.cancelCurrentAction();
                circuits.openSubcircuit(
                    std::move(saved.design.scene), saved.identity, saved.design.name, *path, input
                );
            }
            catch (const std::exception& error)
            {
                m_ui.setFileStatus(error.what(), true);
            }
            input.setUiCapture({});
        }
        if (const auto id = m_ui.takeEditSubcircuit())
        {
            try
            {
                const auto* source = circuits.activeScene().getComponentCatalog().find(*id);
                if (!source)
                    throw std::invalid_argument("Subcircuit definition no longer exists.");
                const auto definition = *source;
                m_ui.dismissPopups(input);
                input.cancelCurrentAction();
                circuits.editSubcircuit(definition, input);
            }
            catch (const std::exception& error)
            {
                m_ui.setFileStatus(error.what(), true);
            }
            input.setUiCapture({});
            input.setCanvasFocused(true);
        }
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
        if (m_showDebugOverlay != m_ui.debugVisible())
        {
            m_showDebugOverlay = m_ui.debugVisible();
            m_cpuTimingWindow.reset();
            m_renderer.invalidateDebugOverlay();
        }
        const auto context = std::pair{circuits.activeIndex(), m_ui.activeBottomTab()};
        if (timingContext != context)
        {
            timingContext = context;
            m_cpuTimingWindow.reset();
            m_renderer.invalidateDebugOverlay();
        }
        CpuTimings cpu;
        auto phaseStart = m_showDebugOverlay ? std::chrono::steady_clock::now()
                                             : std::chrono::steady_clock::time_point{};
        auto finishPhase = [&]()
        {
            if (!m_showDebugOverlay)
                return 0.0;
            const auto now = std::chrono::steady_clock::now();
            const double ms = std::chrono::duration<double, std::milli>(now - phaseStart).count();
            phaseStart = now;
            return ms;
        };

        // 1. Process OS user inputs
        updateUiLayout();
        input.process(window);
        m_ui.update(currentFrameTime, input);
        Scene& parentScene = circuits.activeScene();
        if (displayedMode != input.getMode() || displayedReadOnly != input.isReadOnly())
        {
            displayedMode = input.getMode();
            displayedReadOnly = input.isReadOnly();
            glfwSetWindowTitle(window, titleForMode().c_str());
        }

        cpu[CpuPhase::Input] = finishPhase();

        // Clocks settle the circuit at every transition; remember edits for visual synchronization.
        const bool pendingSimulation = parentScene.isSimulationDirty();
        const bool simulationReset = m_ui.takeSimulationReset();
        bool clockEdgeFlipped =
            !simulationReset && !unsaved.pending() &&
            parentScene.updateClocks(deltaTime, &circuits.activeClockTimeline());

        const bool syncNeeded =
            clockEdgeFlipped || pendingSimulation || parentScene.isSimulationDirty();
        if (parentScene.isSimulationDirty())
            parentScene.propagate();
        parentScene.recordSignalTraces(circuits.activeClockTimeline());
        cpu[CpuPhase::Simulation] = finishPhase();

        if (syncNeeded)
            parentScene.syncVisuals();

        circuits.refreshInspection(input);
        Scene& scene = circuits.displayedScene();

        // Prepare the state used by both the canvas and the UI.
        m_ui.reportSimulation(parentScene, currentFrameTime);

        const auto components = buildComponentPresentation(scene.getComponentViewMap());
        const auto junctions = scene.getWireIntersections();
        CanvasFrame frame{components, scene.getVisibleWires(), junctions};
        std::vector<ComponentRenderData> pasteComponents;
        std::vector<BodyBounds> overlappingPasteBodies, blockedPasteBodies;
        const auto& paste = input.getPastePreview();
        if (const auto* preview = paste.scene())
        {
            pasteComponents = buildComponentPresentation(preview->getComponentViewMap());
            frame.pasteComponents = pasteComponents;
            frame.pasteWires = &preview->getVisibleWires();
            for (int id : paste.overlappingComponents())
                if (const auto* view = preview->getComponentViewMap().at(id).get())
                    if (!paste.blockedComponents().contains(id))
                        overlappingPasteBodies.push_back(view->getBodyBounds());
            for (int id : paste.blockedComponents())
                blockedPasteBodies.push_back(
                    preview->getComponentViewMap().at(id)->getBodyBounds()
                );
            frame.overlappingPasteBodies = overlappingPasteBodies;
            frame.blockedPasteBodies = blockedPasteBodies;
        }
        const float selectionOpacity = input.getSelectionHighlightOpacity();
        std::vector<BodyHighlight> selectedBodies;
        std::vector<SegmentHighlight> selectedSegments;
        for (int id : input.getHighlightedComponents())
            if (const auto* view = scene.getComponentView(id))
                selectedBodies.push_back({view->getBodyBounds(), selectionOpacity});
        for (WireId id : input.getHighlightedWires())
        {
            auto found = scene.getVisibleWires().find(id);
            if (found == scene.getVisibleWires().end())
                continue;
            const auto& path = found->second.getPath();
            for (std::size_t i = 1; i < path.size(); ++i)
                selectedSegments.push_back({path[i - 1], path[i], selectionOpacity});
        }
        frame.selectedBodies = selectedBodies;
        frame.selectedSegments = selectedSegments;
        frame.selectionBox = input.getSelectionBox();
        const int hovered = input.getHoveredComponentId();
        if (!input.getHighlightedComponents().contains(hovered))
            if (const auto* view = scene.getComponentView(hovered))
                frame.bodyHighlight = BodyHighlight{view->getBodyBounds(), 0.4f};
        if (!input.isCurrentlyDrawingWire())
        {
            if (!frame.selectionBox && input.hasSelectedSegment())
                frame.segmentHighlight = SegmentHighlight{
                    input.getSelectedSegmentStart(), input.getSelectedSegmentEnd(), 1
                };
            else if (
                input.hasHoveredSegment() &&
                !input.getHighlightedWires().contains(input.getHoveredWireId())
            )
                frame.segmentHighlight = SegmentHighlight{
                    input.getHoveredSegmentStart(), input.getHoveredSegmentEnd(), 0.4f
                };
        }
        std::optional<Wire> active;
        if (const auto start = input.getWireStartPoint())
            frame.wireStartHighlight = GridHighlight{*start, 0.9f};
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
            input.shouldShowGridPointHighlight() && input.getHoveredComponentId() == -1 &&
            input.getHoveredPinComponentId() == -1
        )
            frame.gridHighlight = GridHighlight{input.getCurrentGridCoords(), 0.55f};
        if (input.shouldShowGridPointHighlight())
        {
            frame.hoveredComponent = input.getHoveredPinComponentId();
            frame.hoveredPin = input.getHoveredPinIndex();
            frame.hoveredDirection = input.getHoveredPinType();
        }
        cpu[CpuPhase::VisualPreparation] = finishPhase();
        m_renderer.beginFrame(input.getCameraFrame(window));
        m_renderer.drawCanvas(frame);
        cpu[CpuPhase::Canvas] = finishPhase();
        m_renderer.beginUiTiming(m_showDebugOverlay);
        m_ui.draw(m_renderer, scene, input.getCameraFrame(window));
        const auto uiCpu = m_renderer.endUiTiming();
        cpu[CpuPhase::StaticUI] = uiCpu[CpuPhase::StaticUI];
        cpu[CpuPhase::DynamicUI] = uiCpu[CpuPhase::DynamicUI];
        finishPhase(); // UI was measured independently at category boundaries.
        const auto work = parentScene.takeSimulationWork();
        cpu[CpuPhase::Propagation] = work.propagationMs;

        // Diagnostics stay above the canvas, panels and popups.
        if (m_showDebugOverlay)
        {
            DebugMetrics metrics;
            metrics.fps = m_fps;
            metrics.frameTimeMs = (deltaTime > 0.0f) ? (deltaTime * 1000.0f) : 0.0f;
            metrics.drawCalls = m_renderer.getDrawCallCounts();
            metrics.averageCpu = m_cpuTimingWindow.average();
            metrics.worstCpu = m_cpuTimingWindow.worst();
            metrics.timingsReady = m_cpuTimingWindow.ready();
            metrics.simulationWork = work;
            metrics.pendingSimulationMs = parentScene.pendingClockTime() * 1000;
            metrics.totalComponents = parentScene.getComponentCount();
            metrics.evalResult = parentScene.getLastEvalResult();

            // Topology metrics
            metrics.netCount = parentScene.netCount();
            metrics.wireCount = parentScene.wireCount();
            metrics.shortedNetCount = parentScene.getShortedNetCount();
            metrics.rejectedConnectionCount = parentScene.getRejectedConnections().size();

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

        cpu[CpuPhase::DebugOverlay] = finishPhase();
        glfwSwapBuffers(window);
        cpu[CpuPhase::FrameWait] = finishPhase();
        glfwPollEvents();
        cpu[CpuPhase::Input] += finishPhase();
        // Do not mix costs from different scenes or bottom-panel tabs in one window.
        if (m_showDebugOverlay &&
            context == std::pair{circuits.activeIndex(), m_ui.activeBottomTab()})
            m_cpuTimingWindow.add(cpu, deltaTime);
    }

    m_ui.cancel(input);
    m_ui.setCircuitViews(nullptr, nullptr);
    input.setUiInputHandler({});
    input.setEditErrorHandler({});
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
