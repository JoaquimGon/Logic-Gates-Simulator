#include "Components/Definitions/NativeDefinitions.h"
#include "Editor/Actions/EditorActions.h"
#include "Editor/CircuitViews.h"
#include "Editor/Input.h"
#include "Editor/Subcircuits.h"
#include "Graphics/Text/TextGeometry.h"
#include "Persistence/CircuitFile.h"
#include "Simulation/Subcircuit.h"
#include "UI/UI.h"

#include <GLFW/glfw3.h>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace
{
void require(bool value, const char* message)
{
    if (!value)
        throw std::runtime_error(message);
}

class EdgeProbe : public Component
{
  public:
    EdgeProbe() : Component(-1, 1, 1) {}

    std::unique_ptr<Component> clone() const override { return std::make_unique<EdgeProbe>(*this); }

    int clockInputPin() const override { return 0; }

    void onClockEdge(bool high) override { edges.push_back(high); }

    void evaluate() override { setStateOutPin(0, getStateInPin(0)); }

    std::vector<bool> edges;
};

class FailingReceiver : public EdgeProbe
{
  public:
    std::unique_ptr<Component> clone() const override
    {
        return std::make_unique<FailingReceiver>(*this);
    }

    SimulationResult simulationResult() const override
    {
        return getStateInPin(0) ? SimulationResult::NON_CONVERGENT : SimulationResult::OK;
    }
};

void steps()
{
    Circuit circuit;
    ClockTimeline timeline;
    const int clock = circuit.addClock(), disabled = circuit.addClock(4);
    static_cast<Clock*>(circuit.getComponent(disabled))->setPaused(true);
    const int receiver = circuit.addComponent(std::make_unique<EdgeProbe>());
    circuit.connectComponents(clock, 0, receiver, 0);
    auto* probe = static_cast<EdgeProbe*>(circuit.getComponent(receiver));
    require(
        circuit.stepClocks(1, &timeline) == 1 && circuit.clocksPaused() &&
            circuit.clockTime() == 0.5 && probe->getStateOutPin(0) &&
            probe->edges == std::vector<bool>{true},
        "Single step did not settle the rising edge."
    );
    require(
        circuit.stepClocks(2, &timeline) == 2 && circuit.clockTime() == 1.5 &&
            probe->edges == std::vector<bool>{true, false, true} && probe->getStateOutPin(0),
        "Double step skipped the intermediate edge/settled state."
    );
    require(
        timeline.traces().at({clock}).size() == 4 && timeline.steps().size() == 3 &&
            timeline.traces().at({disabled}).size() == 1,
        "Step recording lost states or included disabled edges."
    );
    require(
        !circuit.updateClocks(30, &timeline) && circuit.clockTime() == 1.5 &&
            circuit.pendingClockTime() == 0,
        "Global pause accumulated elapsed time."
    );
    circuit.setClocksPaused(false);
    circuit.updateClocks(0.5f, &timeline);
    require(
        !static_cast<Clock*>(circuit.getComponent(clock))->isPaused() &&
            static_cast<Clock*>(circuit.getComponent(disabled))->isPaused() &&
            !probe->getStateOutPin(0),
        "Run changed individual clock settings or lost phase."
    );

    Circuit multiple;
    const int slow = multiple.addClock(1), fast = multiple.addClock(2);
    ClockTimeline simultaneous;
    require(
        multiple.stepClocks(2, &simultaneous) == 2 && multiple.clockTime() == 0.5 &&
            multiple.getComponent(slow)->getStateOutPin(0) &&
            !multiple.getComponent(fast)->getStateOutPin(0) &&
            simultaneous.steps() == std::deque<double>{0.25, 0.5},
        "Several clocks stepped together instead of chronologically."
    );
    static_cast<Clock*>(multiple.getComponent(slow))->setPaused(true);
    static_cast<Clock*>(multiple.getComponent(fast))->setPaused(true);
    require(
        multiple.stepClocks(2) == 0 && multiple.clockTime() == 0.5,
        "Stepping forced disabled clocks."
    );
    Circuit failed;
    const int oscillator = failed.addGate(NOT), blocked = failed.addClock();
    failed.connectComponents(oscillator, 0, oscillator, 0);
    require(
        failed.stepClocks(2) == 0 && !failed.getComponent(blocked)->getStateOutPin(),
        "Manual steps bypassed non-convergence."
    );

    Circuit midStepFailure;
    const int trigger = midStepFailure.addClock();
    const int failing = midStepFailure.addComponent(std::make_unique<FailingReceiver>());
    midStepFailure.connectComponents(trigger, 0, failing, 0);
    ClockTimeline stopped;
    require(
        midStepFailure.stepClocks(2, &stopped) == 1 &&
            midStepFailure.getLastEvalResult() == SimulationResult::NON_CONVERGENT &&
            stopped.steps().size() == 1 && midStepFailure.clockTime() == 0.5,
        "Double step continued after a settling failure."
    );

    Clock phase(0);
    phase.advanceSlice(0.25);
    phase.setFrequency(10);
    require(
        std::abs(phase.timeUntilEdge() - 0.025) < 1e-9,
        "Frequency edit did not preserve the fractional cycle phase."
    );
}

void resetSources()
{
    auto child = std::make_shared<Circuit>();
    const int childInput = child->addInputPin();
    const int childOutput = child->addOutputPin();
    const int innerClock = child->addClock(2);
    child->connectComponents(childInput, 0, childOutput, 0);
    SubcircuitBehavior behavior;
    behavior.circuit = child;
    behavior.inputs = {childInput};
    behavior.outputs = {childOutput};
    Circuit circuit;
    const int manual = circuit.addInputPin(true);
    const int inverter = circuit.addGate(NOT);
    const int clock = circuit.addClock(2);
    const int disabled = circuit.addClock(5);
    auto* disabledClock = static_cast<Clock*>(circuit.getComponent(disabled));
    disabledClock->advanceSlice(0.1);
    disabledClock->setPaused(true);
    const int nested = circuit.addComponent(std::make_unique<Subcircuit>(behavior));
    circuit.connectComponents(manual, 0, inverter, 0);
    circuit.connectComponents(inverter, 0, nested, 0);
    ClockTimeline timeline;
    circuit.updateClocks(0.375f, &timeline);
    circuit.setClocksPaused(true);
    require(
        circuit.getComponent(clock)->getStateOutPin() && circuit.clockReadings()[2].high &&
            !timeline.steps().empty(),
        "Reset fixture did not start clocks HIGH with a recorded phase."
    );
    circuit.resetSources();
    timeline.clear();
    circuit.propagate();
    for (const auto& reading : circuit.clockReadings())
        require(!reading.high, "Reset left a native or nested clock HIGH.");
    require(
        !circuit.getComponent(manual)->getStateOutPin() && circuit.clocksPaused() &&
            disabledClock->isPaused() && disabledClock->getFrequency() == 5 &&
            circuit.clockTime() == 0 && circuit.pendingClockTime() == 0 &&
            timeline.traces().empty() && timeline.steps().empty(),
        "Reset changed clock settings or retained manual inputs/time/history."
    );
    require(
        circuit.getComponent(nested)->getStateOutPin() &&
            child->getComponent(innerClock)->getStateOutPin() == false,
        "Reset corrupted driven subcircuit ports or the shared definition template."
    );
    circuit.stepClocks(1, &timeline);
    require(
        circuit.clockTime() == 0.25 && circuit.getComponent(clock)->getStateOutPin() &&
            circuit.clockReadings()[2].high && !disabledClock->getStateOutPin(),
        "First post-reset edge used stale phase or advanced a disabled clock."
    );
    Circuit backlog;
    const int fast = backlog.addClock(4096);
    backlog.updateClocks(1);
    require(backlog.pendingClockTime() > 0, "Reset backlog fixture did not exceed slice limits.");
    backlog.resetSources();
    backlog.propagate();
    require(
        backlog.pendingClockTime() == 0 && backlog.clockTime() == 0 &&
            !backlog.getComponent(fast)->getStateOutPin() && !backlog.clocksPaused(),
        "Reset retained old catch-up work or paused running clocks."
    );
}

void history()
{
    Scene names;
    for (int i = 0; i < 6; ++i)
        names.addComponent(BuiltinComponentIds::And, {i * 10, 0});
    const int namedFirst = names.addComponent(BuiltinComponentIds::Clock, {-20, 0});
    const int namedSecond = names.addComponent(BuiltinComponentIds::Clock, {-40, 0});
    require(
        namedFirst == 6 && names.clockName({namedFirst}) == "Clock 1" &&
            names.clockName({namedSecond}) == "Clock 2" &&
            names.getCommittedComponentView(namedFirst)->getBodyLabel().empty(),
        "Clock fallbacks exposed component IDs or changed the saved label."
    );
    const auto renamed = EditorActions(names).apply(
        {ConfigureComponentProperties{.componentId = namedFirst, .label = "Reference"}}
    );
    require(
        renamed && names.clockName({namedFirst}) == "Reference" &&
            names.clockName({namedSecond}) == "Clock 2",
        "Clock names gained ID suffixes or numbered non-clock components."
    );
    names.removeComponent(namedFirst);
    require(
        names.clockName({namedSecond}) == "Clock 1", "Fallback numbering included deleted clocks."
    );
    Scene fresh;
    const int freshClock = fresh.addComponent(BuiltinComponentIds::Clock, {0, 0});
    require(
        fresh.clockName({freshClock}) == "Clock 1", "New workspaces inherited clock numbering."
    );

    Circuit circuit;
    ClockTimeline timeline;
    const int clock = circuit.addClock(1000);
    circuit.updateClocks(0.01f, &timeline);
    while (circuit.pendingClockTime() > 0)
        circuit.updateClocks(0, &timeline);
    require(
        timeline.traces().at({clock}).size() >= 20,
        "Waveform was sampled once per frame instead of at each edge."
    );
    for (int i = 0; i < 1100; ++i)
        circuit.stepClocks(2, &timeline);
    require(
        timeline.traces().at({clock}).size() == ClockTimeline::sampleLimit &&
            timeline.steps().size() == ClockTimeline::sampleLimit &&
            timeline.traces().at({clock}).front().time > 0,
        "Clock history was unbounded."
    );
    circuit.delComponent(clock);
    circuit.updateClocks(0, &timeline);
    require(timeline.traces().empty(), "Deleted clock retained its row.");

    auto child = std::make_shared<Circuit>();
    const int internal = child->addClock(2);
    SubcircuitBehavior behavior;
    behavior.circuit = child;
    Circuit parent;
    const int a = parent.addComponent(std::make_unique<Subcircuit>(behavior));
    parent.stepClocks();
    const int b = parent.addComponent(std::make_unique<Subcircuit>(behavior));
    ClockTimeline nested;
    parent.updateClocks(0, &nested);
    const auto readings = parent.clockReadings();
    require(
        readings.size() == 2 && readings[0].path == ClockPath{a, internal} &&
            readings[1].path == ClockPath{b, internal} && readings[0].high && !readings[1].high &&
            !child->getComponent(internal)->getStateOutPin(0) && nested.traces().size() == 2,
        "Nested rows read the authored template or shared instance state."
    );
    parent.stepClocks(1, &nested);
    require(
        !parent.clockReadings()[0].high && parent.clockReadings()[1].high,
        "Nested live clocks did not share the root's scheduled stepping."
    );
}

void previewPanel(GLFWwindow* window)
{
    CircuitViews views;
    Input input;
    UI ui;
    FontMetrics font;
    views.select(0, input);
    ui.setCircuitViews(&views, &font);
    auto& scene = views.activeScene();
    const int id = scene.addComponent(BuiltinComponentIds::Clock, {0, 0});
    ui.layout(scene.getComponentCatalog(), {800, 800, 800, 800}, input);
    const auto frame = input.getCameraFrame(window);
    auto click = [&](CanvasViewport bounds)
    {
        UiInputEvent event{UiInputKind::MouseButton};
        event.code = GLFW_MOUSE_BUTTON_LEFT;
        event.x = bounds.x + bounds.width / 2;
        event.y = bounds.y + bounds.height / 2;
        event.action = GLFW_PRESS;
        ui.handleInput(event, scene, input, frame);
        event.action = GLFW_RELEASE;
        ui.handleInput(event, scene, input, frame);
    };
    click(ui.bottomTabBounds(UI::BottomTab::Clocks));
    require(ui.clockStartTime() == -2, "The initial current-time marker was not centred.");
    auto& timeline = views.activeClockTimeline();
    scene.updateClocks(0.25f, &timeline);
    const auto before = timeline.traces().at({id});
    const auto reading = scene.clockReadings().front();
    const auto originalWindow = ui.clockWindowSeconds();
    const auto originalStart = ui.clockStartTime();
    const auto predicted = ui.clockPreview(reading);
    require(
        ui.clockStartTime() + ui.clockWindowSeconds() / 2 == timeline.time() &&
            reading.timeUntilEdge == 0.25 && predicted.front().time == 0.25 &&
            !predicted.front().high && predicted[1].time == 0.5 && predicted[1].high &&
            scene.clockTime() == 0.25 && !scene.getLogicComponent(id)->getStateOutPin(),
        "Clock preview ignored the live phase, moved the cursor or changed simulation."
    );
    scene.togglePauseAllClocks();
    require(
        ui.clockPreview(scene.clockReadings().front()).size() > 1,
        "Globally paused clocks lost their scheduled preview."
    );
    require(
        static_cast<bool>(EditorActions(scene).apply(
            {ConfigureComponentProperties{.componentId = id, .clockFrequency = 2.0f}}
        )),
        "Cannot edit preview frequency."
    );
    const auto changed = ui.clockPreview(scene.clockReadings().front());
    ui.layout(scene.getComponentCatalog(), {800, 800, 800, 800}, input);
    require(
        changed[1].time == 0.375 && changed[2].time == 0.625 &&
            ui.clockWindowSeconds() == originalWindow && ui.clockStartTime() == originalStart &&
            timeline.traces().at({id}).size() == before.size() &&
            timeline.traces().at({id}).front().time == before.front().time &&
            timeline.traces().at({id}).back().time == before.back().time,
        "Hz changes reset phase, kept an old forecast or modified recorded history."
    );
    click(ui.clockPauseBounds(0));
    require(
        ui.clockPreview(scene.clockReadings().front()).size() == 1 &&
            ui.clockWindowSeconds() == originalWindow,
        "A disabled clock preview invented future transitions."
    );
    click(ui.clockPauseBounds(0));
    click(ui.clockControlBounds(UI::ClockControl::ZoomOut));
    // Zoom stays explicit; later Fit will use the new configured frequency.
    const auto manualWindow = ui.clockWindowSeconds();
    const ClockReading fast{{id}, 1e8f, false, false, 1e-9};
    require(ui.clockPreview(fast).size() == 129, "Preview geometry is not bounded to 128 edges.");

    scene.togglePauseAllClocks();
    scene.updateClocks(5, &timeline);
    while (scene.pendingClockTime() > 0)
        scene.updateClocks(0, &timeline);
    const auto bounds = ui.clockListBounds();
    UiInputEvent hover{UiInputKind::Cursor};
    hover.x = bounds.x + bounds.width - 10;
    hover.y = bounds.y + 10;
    ui.handleInput(hover, scene, input, frame);
    UiInputEvent scroll{UiInputKind::Scroll};
    scroll.y = 2;
    scroll.modifiers = GLFW_MOD_SHIFT;
    ui.handleInput(scroll, scene, input, frame);
    const double browsed = ui.clockStartTime();
    scene.updateClocks(1, &timeline);
    require(
        ui.clockStartTime() == browsed && ui.clockWindowSeconds() == manualWindow,
        "Inspecting history snapped back to live time or changed zoom."
    );
    click(ui.clockControlBounds(UI::ClockControl::Live));
    require(
        ui.clockStartTime() + manualWindow / 2 == timeline.time(),
        "Live did not restore the centred cursor at the existing zoom."
    );
    click(ui.bottomTabBounds(UI::BottomTab::Signals));
    require(
        ui.clockStartTime() + ui.clockWindowSeconds() == timeline.time(),
        "Signals moved current time away from the right edge of recorded history."
    );
    click(ui.clockControlBounds(UI::ClockControl::ZoomOut));
    click(ui.bottomTabBounds(UI::BottomTab::Clocks));
    require(
        ui.clockWindowSeconds() == manualWindow,
        "Changing the Signals time frame rescaled the Clocks history."
    );
    click(ui.clockControlBounds(UI::ClockControl::Clear));
    require(
        ui.clockStartTime() + manualWindow / 2 == timeline.time() &&
            ui.clockPreview(scene.clockReadings().front()).front().time == scene.clockTime(),
        "Clear left the preview anchored to old history."
    );
    ui.setCircuitViews(nullptr, nullptr);
    input.setScene(nullptr);
}

void panel(GLFWwindow* window)
{
    Input input;
    CircuitViews views;
    UI ui;
    FontMetrics font;
    for (auto& glyph : font.cdata)
        glyph.xadvance = 18;
    views.select(0, input);
    glfwSetWindowUserPointer(window, &input);
    ui.setCircuitViews(&views, &font);
    const int clock = views.activeScene().addComponent(BuiltinComponentIds::Clock, {0, 0});
    views.markSaved(0);
    auto layout = [&]
    { ui.layout(views.activeScene().getComponentCatalog(), {800, 800, 800, 800}, input); };
    layout();
    input.setUiInputHandler(
        [&](const UiInputEvent& event)
        { return ui.handleInput(event, views.activeScene(), input, input.getCameraFrame(window)); }
    );
    auto click = [&](CanvasViewport box)
    {
        const double x = box.x + box.width / 2, y = box.y + box.height / 2;
        glfwSetCursorPos(window, x, y);
        input.handleCursorPos(window, x, y);
        input.handleMouseButton(window, GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS, 0);
        input.handleMouseButton(window, GLFW_MOUSE_BUTTON_LEFT, GLFW_RELEASE, 0);
    };
    auto key = [&](int code, int modifiers = 0)
    {
        input.handleKey(code, GLFW_PRESS, modifiers);
        input.handleKey(code, GLFW_RELEASE, modifiers);
    };
    click(ui.bottomTabBounds(UI::BottomTab::Clocks));
    require(ui.activeBottomTab() == UI::BottomTab::Clocks, "Workspace has no Clocks tab.");
    const auto stepButton = ui.clockControlBounds(UI::ClockControl::SingleStep);
    input.handleCursorPos(window, stepButton.x + 2, stepButton.y + 2);
    ui.update(10, input);
    require(ui.clockTooltip(views.activeScene()).empty(), "Clock tooltip appeared without delay.");
    ui.update(10.8, input);
    const auto stepTip = ui.clockTooltip(views.activeScene());
    require(
        !stepTip.empty() && stepTip[1].find("any enabled clock") != std::string::npos,
        "Step tooltip did not explain the shared timeline."
    );
    click(ui.clockControlBounds(UI::ClockControl::SingleStep));
    require(
        views.activeScene().clocksPaused() && views.activeScene().clockTime() == 0.5 &&
            views.activeClockTimeline().steps().size() == 1 && !views.hasUnsavedChanges(0),
        "Panel step did not run or dirtied the saved design."
    );
    click(ui.clockControlBounds(UI::ClockControl::DoubleStep));
    require(
        views.activeScene().clockTime() == 1.5 && views.activeClockTimeline().steps().size() == 3,
        "Double-step control skipped recording."
    );
    const auto zoom = input.getZoom();
    click(ui.clockControlBounds(UI::ClockControl::ZoomIn));
    require(
        ui.clockWindowSeconds() == 2 && input.getZoom() == zoom,
        "Timeline zoom affected the canvas."
    );
    click(ui.clockControlBounds(UI::ClockControl::Fit));
    require(
        ui.clockWindowSeconds() == 4 && ui.clockTimeFrameText() == "Time frame = 4 s",
        "Fit did not show four cycles or label the displayed time frame."
    );
    click(ui.clockFrequencyBounds(0));
    input.handleText('8');
    key(GLFW_KEY_ENTER);
    require(
        static_cast<const Clock*>(views.activeScene().getLogicComponent(clock))->getFrequency() ==
                8 &&
            input.getUndoCount() == 1 && views.hasUnsavedChanges(0) && ui.clockWindowSeconds() == 4,
        "Frequency edit was not undoable/savable."
    );
    input.setCanvasFocused(true);
    key(GLFW_KEY_Z, GLFW_MOD_CONTROL);
    require(
        static_cast<const Clock*>(views.activeScene().getLogicComponent(clock))->getFrequency() ==
                1 &&
            views.activeClockTimeline().traces().empty(),
        "Undo retained incompatible trace history."
    );
    click(ui.clockFrequencyBounds(0));
    input.handleText('0');
    key(GLFW_KEY_ENTER);
    require(
        input.getUiCapture().keyboard &&
            static_cast<const Clock*>(views.activeScene().getLogicComponent(clock))
                    ->getFrequency() == 1,
        "Invalid frequency changed simulation or released typing capture."
    );
    key(GLFW_KEY_ESCAPE);
    click(ui.clockPauseBounds(0));
    const auto samples = views.activeClockTimeline().steps().size();
    const auto doubleButton = ui.clockControlBounds(UI::ClockControl::DoubleStep);
    input.handleCursorPos(window, doubleButton.x + 2, doubleButton.y + 2);
    ui.update(12, input);
    ui.update(12.8, input);
    const auto disabledTip = ui.clockTooltip(views.activeScene());
    require(
        std::any_of(
            disabledTip.begin(),
            disabledTip.end(),
            [](const auto& text) { return text.find("All clocks disabled") != std::string::npos; }
        ),
        "Disabled stepping did not have a hover explanation."
    );
    click(ui.clockControlBounds(UI::ClockControl::SingleStep));
    require(
        views.activeClockTimeline().steps().size() == samples &&
            static_cast<const Clock*>(views.activeScene().getLogicComponent(clock))->isPaused(),
        "Panel stepping ignored a disabled clock."
    );
    click(ui.clockPauseBounds(0));
    click(ui.clockControlBounds(UI::ClockControl::SingleStep));
    const auto time = views.activeScene().clockTime();
    const bool high = views.activeScene().getLogicComponent(clock)->getStateOutPin();
    const auto undoCount = input.getUndoCount();
    click(ui.clockControlBounds(UI::ClockControl::Clear));
    require(
        views.activeClockTimeline().steps().empty() &&
            views.activeClockTimeline().traces().at({clock}).size() == 1 &&
            views.activeClockTimeline().traces().at({clock}).front().time == time &&
            views.activeScene().clockTime() == time &&
            views.activeScene().getLogicComponent(clock)->getStateOutPin() == high &&
            input.getUndoCount() == undoCount,
        "Clear reset the simulation or retained old graph data."
    );
    click(ui.clockControlBounds(UI::ClockControl::SingleStep));
    const auto mainSteps = views.activeClockTimeline().steps().size();
    views.create(input, CircuitViews::Role::Subcircuit);
    layout();
    require(
        ui.bottomTabBounds(UI::BottomTab::Clocks).width > 0 &&
            ui.bottomTabBounds(UI::BottomTab::Subcircuit).width > 0 &&
            views.activeClockTimeline().traces().empty(),
        "Subcircuit tab inherited workspace traces."
    );
    views.select(0, input);
    layout();
    require(
        views.activeClockTimeline().steps().size() == mainSteps,
        "Switching tabs lost the outgoing timeline."
    );
    Scene authored;
    const int gap = authored.addComponent(BuiltinComponentIds::And, {60, 60});
    authored.removeComponent(gap);
    authored.addComponent(BuiltinComponentIds::Input, {-20, 0});
    authored.addComponent(BuiltinComponentIds::Output, {20, 0});
    const int namedClock = authored.addComponent(BuiltinComponentIds::Clock, {0, 20});
    authored.setInterfaceNamingRequired(true);
    const auto named = EditorActions(authored).apply(
        {ConfigureComponentProperties{.componentId = namedClock, .label = "Pulse"}}
    );
    require(static_cast<bool>(named), "Nested clock naming fixture failed.");
    const auto definition = makeSubcircuit(authored, {"subcircuit.clock-panel", 1}, "Timer");
    views.publishSubcircuits({definition});
    const int nestedId = views.activeScene().addComponent(definition.identity.id, {20, 20});
    click(ui.bottomTabBounds(UI::BottomTab::Clocks));
    require(
        views.activeScene().clockName({nestedId, 2}) == "Timer 1/Pulse",
        "Nested row labels did not map runtime IDs to authored labels."
    );
    const int secondNested = views.activeScene().addComponent(definition.identity.id, {40, 20});
    require(
        views.activeScene().clockName({secondNested, 2}) == "Timer 2/Pulse",
        "Repeated subcircuits were not distinguishable without exposing internal IDs."
    );
    const auto count = input.getUndoCount();
    UiInputEvent scroll{UiInputKind::Scroll};
    scroll.y = -1;
    ui.handleInput(scroll, views.activeScene(), input, input.getCameraFrame(window));
    click(ui.clockPauseBounds(0));
    click(ui.clockFrequencyBounds(0));
    require(
        !input.getUiCapture().keyboard && input.getUndoCount() == count,
        "Nested row allowed editing the template from the runtime viewer."
    );

    ui.dismissPopups(input);
    views.create(input);
    const int fast = views.activeScene().addComponent(
        BuiltinComponentIds::Clock, {0, 0}, ComponentOverrides{.clockFrequency = 256.0f}
    );
    views.activeScene().addComponent(
        BuiltinComponentIds::Clock, {20, 0}, ComponentOverrides{.clockFrequency = 1.0f}
    );
    layout();
    click(ui.bottomTabBounds(UI::BottomTab::Clocks));
    require(
        ui.clockWindowSeconds() == 4.0 / 256 &&
            ui.clockTimeFrameText() == "Time frame = 0.015625 s",
        "High-frequency clocks did not automatically receive a readable time frame."
    );
    click(ui.clockControlBounds(UI::ClockControl::ZoomOut));
    const auto manualWindow = ui.clockWindowSeconds();
    for (int i = 0; i < 7; ++i)
        click(ui.clockControlBounds(UI::ClockControl::ZoomOut));
    require(
        ui.clockWindowSeconds() == 4 && ui.clockWaveformTooDense(256),
        "256 Hz over four seconds was not suppressed."
    );
    ui.layout(views.activeScene().getComponentCatalog(), {1900, 800, 1900, 800}, input);
    require(ui.clockWaveformTooDense(256), "Wide windows allowed thousands of clock edges.");
    click(ui.clockControlBounds(UI::ClockControl::Fit));
    require(!ui.clockWaveformTooDense(256), "Fit did not restore a readable waveform.");
    layout();
    click(ui.clockControlBounds(UI::ClockControl::ZoomOut));
    const auto rate =
        EditorActions(views.activeScene())
            .apply({ConfigureComponentProperties{.componentId = fast, .clockFrequency = 1024.0f}});
    require(
        static_cast<bool>(rate) && ui.clockWindowSeconds() == manualWindow,
        "Frequency changes overwrote manual zoom."
    );
    click(ui.clockControlBounds(UI::ClockControl::Fit));
    require(ui.clockWindowSeconds() == 4.0 / 1024, "Fit did not explicitly rescale the graph.");
    click(ui.clockPauseBounds(0));
    require(ui.clockWindowSeconds() == 4.0 / 1024, "Disabling a clock rescaled recorded history.");
    click(ui.clockControlBounds(UI::ClockControl::Fit));
    require(ui.clockWindowSeconds() == 4, "Fit did not use the fastest enabled clock.");

    auto loaded = views.activeScene();
    views.replaceWorkspace(std::move(loaded), "Opened", input);
    require(
        views.activeClockTimeline().traces().empty(), "Opening a scene retained its old trace."
    );
    ui.dismissPopups(input);
    // Reset from the navbar must work in Interaction mode and affect this view only.
    views.select(0, input);
    const int manual = views.activeScene().addComponent(BuiltinComponentIds::Input, {20, 0});
    static_cast<InputPin*>(views.activeScene().getLogicComponent(manual))->setState(true);
    views.activeScene().propagate();
    views.markSaved(0);
    views.activeScene().stepAllClocks(1, &views.activeClockTimeline());
    const auto frequency =
        static_cast<const Clock*>(views.activeScene().getLogicComponent(clock))->getFrequency();
    const auto paused = views.activeScene().clocksPaused();
    const auto cameraPan = input.getPanOffset();
    input.setMode(EditorMode::Interaction);
    layout();
    click(ui.menuBounds(UI::Menu::Simulation));
    require(ui.menuOpen(UI::Menu::Simulation), "Simulation menu did not open.");
    click(ui.menuOptionBounds(UI::Menu::Simulation, 0));
    ui.update(50, input);
    require(
        !views.activeScene().getLogicComponent(clock)->getStateOutPin() &&
            !views.activeScene().getLogicComponent(manual)->getStateOutPin() &&
            views.activeScene().clockTime() == 0 && views.activeClockTimeline().traces().empty() &&
            views.activeClockTimeline().steps().empty() &&
            views.activeScene().clocksPaused() == paused &&
            static_cast<const Clock*>(views.activeScene().getLogicComponent(clock))
                    ->getFrequency() == frequency &&
            input.getPanOffset() == cameraPan && views.hasUnsavedChanges(0) &&
            ui.takeSimulationReset() && !ui.takeSimulationReset(),
        "Navbar reset lost settings/camera or retained signals/time/traces."
    );
    input.setUiInputHandler({});
    input.setScene(nullptr);
    glfwSetWindowUserPointer(window, nullptr);
}
} // namespace

int main(int argc, char** argv)
{
    try
    {
        const std::string test = argc > 1 ? argv[1] : "";
        if (test == "clock_steps")
            steps();
        else if (test == "clock_reset")
            resetSources();
        else if (test == "clock_history")
            history();
        else if (test == "clock_panel" || test == "clock_preview")
        {
            glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_NULL);
            require(glfwInit(), "Cannot initialize headless GLFW.");
            glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
            auto* window = glfwCreateWindow(800, 800, "Clock controls", nullptr, nullptr);
            require(window, "Cannot create headless window.");
            if (test == "clock_preview")
                previewPanel(window);
            else
                panel(window);
            glfwDestroyWindow(window);
            glfwTerminate();
        }
        else
            throw std::runtime_error("Unknown clock test.");
        std::cout << "PASS: " << test << '\n';
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
