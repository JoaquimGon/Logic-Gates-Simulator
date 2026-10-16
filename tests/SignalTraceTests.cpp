#include "Components/Definitions/NativeDefinitions.h"
#include "Components/FlipFlop.h"
#include "Editor/Actions/EditorActions.h"
#include "Editor/CircuitViews.h"
#include "Editor/Input.h"
#include "Editor/Subcircuits.h"
#include "Graphics/Text/TextGeometry.h"
#include "Persistence/CircuitFile.h"
#include "Simulation/ClockTimeline.h"
#include "UI/UI.h"

#include <GLFW/glfw3.h>
#include <iostream>
#include <stdexcept>

namespace
{
void require(bool value, const char* message)
{
    if (!value)
        throw std::runtime_error(message);
}


void edit(Scene& scene, const EditBatch& batch)
{
    const auto result = EditorActions(scene).apply(batch);
    if (!result)
        throw std::runtime_error(result.message);
}


/** @brief Verify nested pin selections retain complete bounded level and availability histories across simulation
 * changes. */
void recording()
{
    Circuit circuit;
    const int data = circuit.addInputPin(true), clock = circuit.addClock(2);
    const int ff = circuit.addComponent(std::make_unique<FlipFlop>(-1, FlipFlopType::D));
    require(
        circuit.connectComponents(data, 0, ff, 0) && circuit.connectComponents(clock, 0, ff, 1),
        "Fixture wiring failed."
    );
    ClockTimeline timeline;
    for (auto [id, pin] : {std::pair{clock, 0u}, std::pair{ff, 0u}, std::pair{ff, 1u}})
        require(timeline.toggleSignal({{id}, PinType::OUTPUT, pin, "", "signal", {}}), "Cannot add probe.");
    require(timeline.toggleSignal({{ff}, PinType::INPUT, 0, "", "D", {}}), "Cannot add input probe.");
    circuit.updateClocks(0.5f, &timeline);
    while (circuit.pendingClockTime() > 0)
        circuit.updateClocks(0, &timeline);
    const auto& clk = timeline.signals()[0].samples;
    require(
        clk.size() == 3 && clk[1].time == 0.25 && clk[1].high && !clk[2].high,
        "Recording missed intermediate clock edges within one frame."
    );
    require(
        timeline.signals()[1].samples.size() == 2 && timeline.signals()[1].samples.back().high &&
            !timeline.signals()[2].samples.back().high,
        "Q and ~Q did not record independently."
    );
    circuit.setClocksPaused(true);
    static_cast<InputPin*>(circuit.getComponent(data))->setState(false);
    circuit.markStateDirty();
    circuit.updateClocks(0.2f, &timeline);
    require(
        circuit.clockTime() == 0.5 && !timeline.signals()[3].samples.back().high &&
            timeline.signals()[1].samples.back().high,
        "Paused manual changes sampled unsettled state or advanced time."
    );
    circuit.stepClocks(1, &timeline);
    require(
        !timeline.signals()[1].samples.back().high && timeline.signals()[2].samples.back().high,
        "Single stepping did not capture settled flip-flop output."
    );
    timeline.clear();
    require(
        timeline.signals().size() == 4 && timeline.signals()[0].samples.empty() && circuit.clockTime() == 0.75,
        "Clear removed selected probes or changed simulation time."
    );
    ClockTimeline bounded;
    require(bounded.toggleSignal({{data}, PinType::OUTPUT, 0, "", "D", {}}), "Cannot add bounded probe.");
    for (int i = 0; i < 2100; ++i)
    {
        static_cast<InputPin*>(circuit.getComponent(data))->setState(i % 2);
        circuit.markStateDirty();
        circuit.propagate();
        bounded.recordSignals(circuit);
    }
    require(bounded.signals()[0].samples.size() == ClockTimeline::sampleLimit, "Signal history is unbounded.");
    for (int i = 0; i < 7; ++i)
        require(
            bounded.toggleSignal({{circuit.addInputPin()}, PinType::OUTPUT, 0, "", "probe", {}}),
            "Probe rejected below limit."
        );
    require(
        !bounded.toggleSignal({{circuit.addInputPin()}, PinType::OUTPUT, 0, "", "extra", {}}),
        "Probe limit not enforced."
    );
    circuit.delComponent(data);
    bounded.recordSignals(circuit);
    require(bounded.signals().size() == 7, "Deleted component retained a probe.");
    Circuit oscillator;
    const int gate = oscillator.addGate(NOT);
    oscillator.connectComponents(gate, 0, gate, 0);
    oscillator.propagate();
    ClockTimeline invalid;
    invalid.toggleSignal({{gate}, PinType::OUTPUT, 0, "", "oscillator", {}});
    invalid.recordSignals(oscillator);
    require(!invalid.signals()[0].samples.back().available, "Failed simulation displayed a valid LOW/HIGH trace.");
}


/** @brief Verify signal-panel selection, explicit fitting, scrolling and clear operations preserve intended state. */
void controls(GLFWwindow* window)
{
    CircuitViews views;
    Input input;
    UI ui;
    FontMetrics font;
    views.select(0, input);
    ui.setCircuitViews(&views, &font);
    auto& scene = views.mainScene();
    const int data = scene.addComponent(BuiltinComponentIds::Input, {-12, 2});
    const int clock = scene.addComponent(BuiltinComponentIds::Clock, {-12, -2});
    const int ff = scene.addComponent(BuiltinComponentIds::DFlipFlop, {0, 0});
    edit(scene, {AddWire{{{-11, 2}, {-3, 2}, {-3, 1}}}, AddWire{{{-11, -2}, {-3, -2}, {-3, -1}}}});
    scene.handleClick(data);
    scene.propagate();
    scene.syncVisuals();
    views.markSaved(0);
    auto layout = [&] { ui.layout(views.displayedScene().getComponentCatalog(), {800, 800, 800, 800}, input); };
    layout();
    auto click = [&](CanvasViewport bounds, int button = GLFW_MOUSE_BUTTON_LEFT)
    {
        require(bounds.width > 0 && bounds.height > 0, "Missing trace control.");
        UiInputEvent event{UiInputKind::MouseButton};
        event.code = button;
        event.x = bounds.x + bounds.width / 2;
        event.y = bounds.y + bounds.height / 2;
        event.action = GLFW_PRESS;
        ui.handleInput(event, views.displayedScene(), input, input.getCameraFrame(window));
        event.action = GLFW_RELEASE;
        ui.handleInput(event, views.displayedScene(), input, input.getCameraFrame(window));
    };
    auto open = [&](int id)
    {
        const auto position = *input.getCameraFrame(window).worldToWindow(
            views.displayedScene().getCommittedComponentView(id)->getPosition()
        );
        click({position.x - 1, position.y - 1, 2, 2}, GLFW_MOUSE_BUTTON_RIGHT);
    };
    const auto saved = circuitToJson(scene, "Main");
    const auto revision = scene.getRevision();
    input.setMode(EditorMode::Interaction);
    layout();
    open(ff);
    click(ui.signalTraceBounds(scene, PinType::INPUT, 0));
    click(ui.signalTraceBounds(scene, PinType::OUTPUT, 0));
    require(
        views.activeClockTimeline().signals().size() == 2 && circuitToJson(scene, "Main") == saved &&
            scene.getRevision() == revision && input.getUndoCount() == 0 && !views.hasUnsavedChanges(0),
        "Monitoring edited the circuit, entered history or changed saving state."
    );
    ui.dismissPopups(input);
    click(ui.bottomTabBounds(UI::BottomTab::Signals));
    require(ui.activeBottomTab() == UI::BottomTab::Signals, "Signals tab did not open.");
    require(ui.clockWindowSeconds() == 2, "Signals did not start with a fixed two-second frame.");
    click(ui.clockControlBounds(UI::ClockControl::SingleStep));
    require(
        scene.getLogicComponent(ff)->getStateOutPin(0) && views.activeClockTimeline().signals()[1].samples.back().high,
        "Signals controls did not step the owning circuit."
    );
    require(ui.clockWindowSeconds() == 2, "Recording a signal change automatically rescaled Signals.");
    click(ui.clockControlBounds(UI::ClockControl::Fit));
    require(ui.clockWindowSeconds() == 4, "Explicit Fit did not fit the recorded signal intervals.");
    views.activeClockTimeline().toggleSignal(
        {{clock}, PinType::OUTPUT, 0, scene.getCommittedComponentView(clock)->getOutputPins().front().id, "Clock", {}}
    );
    edit(scene, {ConfigureComponentProperties{.componentId = clock, .clockFrequency = 4.0f}});
    scene.recordSignalTraces(views.activeClockTimeline());
    click(ui.clockControlBounds(UI::ClockControl::SingleStep));
    layout();
    require(ui.clockWindowSeconds() == 4, "New probes or faster edges overwrote the fitted frame.");
    click(ui.clockControlBounds(UI::ClockControl::Fit));
    require(ui.clockWindowSeconds() == 1, "Explicit Fit ignored the new shorter signal intervals.");
    views.activeClockTimeline().removeSignal(2);
    const auto time = scene.clockTime();
    click(ui.clockControlBounds(UI::ClockControl::Clear));
    require(
        scene.clockTime() == time && views.activeClockTimeline().signals()[1].samples.size() == 1,
        "Signal clear changed time or failed to establish a fresh baseline."
    );
    click(ui.signalRemoveBounds(0));
    require(views.activeClockTimeline().signals().size() == 1, "X did not remove a probe.");
    scene.removeComponent(ff);
    scene.recordSignalTraces(views.activeClockTimeline());
    require(views.activeClockTimeline().signals().empty(), "UI probe survived deletion.");
    Scene authored;
    const int gap = authored.addComponent(BuiltinComponentIds::And, {40, 40});
    authored.removeComponent(gap);
    const int in = authored.addComponent(BuiltinComponentIds::Input, {-8, 0});
    authored.addComponent(BuiltinComponentIds::Output, {8, 0});
    authored.setInterfaceNamingRequired(true);
    edit(authored, {AddWire{{{-7, 0}, {7, 0}}}});
    const auto definition = makeSubcircuit(authored, {"subcircuit.signals", 1}, "Wire");
    views.publishSubcircuits({definition});
    const int placed = scene.addComponent(definition.identity.id, {0, 8});
    edit(scene, {AddWire{{{-11, 2}, {-6, 2}, {-6, 8}, {-4, 8}}}});
    scene.propagate();
    scene.recordSignalTraces(views.activeClockTimeline());
    require(views.inspectComponent(placed, input), "Cannot inspect nested probe fixture.");
    layout();
    open(in);
    click(ui.signalTraceBounds(views.displayedScene(), PinType::OUTPUT, 0));
    const auto& trace = views.activeClockTimeline().signals().front();
    require(
        trace.path == ClockPath{placed, 0} && trace.samples.back().high && input.isReadOnly(),
        "Live probe sampled the authored design or wrong runtime ID."
    );
    views.closeInspection(input);
    edit(scene, {ConfigureComponentProperties{.componentId = clock, .label = "Edited"}});
    scene.recordSignalTraces(views.activeClockTimeline());
    require(views.activeClockTimeline().signals().empty(), "Parent edit retained a potentially stale nested probe.");
    views.create(input);
    layout();
    require(views.activeClockTimeline().signals().empty(), "Workspace inherited probes.");
    ui.setCircuitViews(nullptr, nullptr);
    input.setScene(nullptr);
}
} // namespace


int main()
{
    glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_NULL);
    if (!glfwInit())
        return 1;
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    auto* window = glfwCreateWindow(800, 800, "Signal traces", nullptr, nullptr);
    int result = 0;
    try
    {
        require(window, "Cannot create headless window.");
        recording();
        controls(window);
        std::cout << "PASS: signal_traces\n";
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        result = 1;
    }
    if (window)
        glfwDestroyWindow(window);
    glfwTerminate();
    return result;
}
