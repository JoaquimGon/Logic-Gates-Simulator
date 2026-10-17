#include "Components/Definitions/NativeDefinitions.h"
#include "Components/FlipFlop.h"
#include "Editor/Actions/EditorActions.h"
#include "Editor/CircuitViews.h"
#include "Editor/Input.h"
#include "Editor/Subcircuits.h"
#include "Geometry/GridSystem.h"
#include "Graphics/Text/TextGeometry.h"
#include "Persistence/CircuitFile.h"
#include "Simulation/Subcircuit.h"
#include "UI/UI.h"

#include <GLFW/glfw3.h>
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <utility>

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


/** @brief Publish a reusable test design through the same catalog validation as normal editor operations. */
ComponentDefinition registerDesign()
{
    Scene authored;
    const int gap = authored.addComponent(BuiltinComponentIds::And, {40, 40});
    authored.removeComponent(gap);
    const int data = authored.addComponent(BuiltinComponentIds::Input, {-12, 2});
    const int clock = authored.addComponent(BuiltinComponentIds::Input, {-12, -2});
    authored.addComponent(BuiltinComponentIds::DFlipFlop, {0, 0});
    const int q = authored.addComponent(BuiltinComponentIds::Output, {12, 2});
    const int notQ = authored.addComponent(BuiltinComponentIds::Output, {12, -2});
    authored.addComponent(BuiltinComponentIds::Clock, {0, 8});
    edit(
        authored, {ConfigureComponentProperties{.componentId = data, .label = "Data"},
                   ConfigureComponentProperties{.componentId = clock, .label = "CLK"},
                   ConfigureComponentProperties{.componentId = q, .label = "Q"},
                   ConfigureComponentProperties{.componentId = notQ, .label = "~Q"},
                   AddWire{{{-11, 2}, {-3, 2}, {-3, 1}}}, AddWire{{{-11, -2}, {-3, -2}, {-3, -1}}},
                   AddWire{{{3, 1}, {11, 1}, {11, 2}}}, AddWire{{{3, -1}, {11, -1}, {11, -2}}}}
    );
    return makeSubcircuit(authored, {"subcircuit.live-register", 1}, "Register");
}


/** @brief Verify nested inspection observes the correct runtime instance and survives edits, switching and invalidated
 * paths. */
void run(GLFWwindow* window)
{
    CircuitViews views;
    Input input;
    UI ui;
    FontMetrics font;
    views.select(0, input);
    ui.setCircuitViews(&views, &font);
    glfwSetWindowUserPointer(window, &input);
    auto layout = [&]
    { ui.layout(views.displayedScene().getComponentCatalog(), input.getCameraFrame(window).surface, input); };
    layout();
    input.setUiInputHandler(
        [&](const UiInputEvent& event)
        {
            if (views.inspecting() && !views.refreshInspection(input, false))
                ui.dismissPopups(input);
            layout();
            const bool consumed = ui.handleInput(event, views.displayedScene(), input, input.getCameraFrame(window));
            layout();
            return consumed;
        }
    );
    auto cursor = [&](GridCoords position)
    {
        const auto point = *input.getCameraFrame(window).worldToWindow(GridSystem::gridToWorld(position));
        glfwSetCursorPos(window, point.x, point.y);
        Input::cursorPositionCallback(window, point.x, point.y);
    };
    auto mouse = [&](int button, int action) { Input::mouseButtonCallback(window, button, action, 0); };
    auto click = [&](CanvasViewport bounds)
    {
        require(bounds.width > 0, "Missing live-inspection button.");
        const double x = bounds.x + bounds.width / 2, y = bounds.y + bounds.height / 2;
        glfwSetCursorPos(window, x, y);
        Input::cursorPositionCallback(window, x, y);
        mouse(GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS);
        mouse(GLFW_MOUSE_BUTTON_LEFT, GLFW_RELEASE);
    };
    auto key = [&](int code, int mods = 0)
    {
        Input::keyCallback(window, code, 0, GLFW_PRESS, mods);
        input.process(window);
        Input::keyCallback(window, code, 0, GLFW_RELEASE, mods);
    };
    auto openInfo = [&](int id)
    {
        if (ui.infoComponentId() != -1)
            key(GLFW_KEY_ESCAPE);
        cursor(views.displayedScene().getCommittedComponentView(id)->getGridPosition());
        mouse(GLFW_MOUSE_BUTTON_RIGHT, GLFW_PRESS);
        mouse(GLFW_MOUSE_BUTTON_RIGHT, GLFW_RELEASE);
        require(ui.infoComponentId() == id, "Live component information did not open.");
    };
    const auto definition = registerDesign();
    auto& parent = views.activeScene();
    edit(parent, {RegisterComponentDefinition{definition}});
    const int data = parent.addComponent(BuiltinComponentIds::Input, {-20, 6});
    parent.addComponent(BuiltinComponentIds::Input, {0, 6});
    const int clock = parent.addComponent(BuiltinComponentIds::Clock, {-20, -6});
    const int first = parent.addComponent(definition.identity.id, {-10, 0});
    const int second = parent.addComponent(definition.identity.id, {10, 0});
    edit(
        parent, {ConfigureInput{data, true}, ConfigureClock{clock, 2, false}, AddWire{{{-19, 6}, {-14, 6}, {-14, 1}}},
                 AddWire{{{1, 6}, {6, 6}, {6, 1}}}, AddWire{{{-19, -6}, {-14, -6}, {-14, -1}}},
                 AddWire{{{-14, -6}, {6, -6}, {6, -1}}}}
    );
    parent.propagate();
    parent.stepAllClocks(1, &views.activeClockTimeline());
    parent.syncVisuals();
    require(
        parent.getLogicComponent(first)->getStateOutPin(0) && !parent.getLogicComponent(second)->getStateOutPin(0),
        "Instances did not store different bits."
    );
    views.markSaved(0);
    const auto saved = circuitToJson(parent, views.name(0));
    const auto revision = parent.getRevision();
    const double time = parent.clockTime();
    parent.takeSimulationWork();
    input.setPanOffset({0.1f, 0.07f});
    input.setZoom(1.25f);
    openInfo(first);
    click(ui.viewInsideBounds(parent));
    require(
        views.inspecting() && input.isReadOnly() && views.size() == 1 && views.inspectionLabel() == "Register 1",
        "Viewer created an editing tab or lost instance identity."
    );
    require(
        parent.clockTime() == time && parent.getRevision() == revision &&
            parent.takeSimulationWork().propagationCalls == 0 && !views.hasUnsavedChanges(0),
        "Entering the viewer advanced, reset or dirtied the parent."
    );
    const auto& displayed = std::as_const(views).displayedScene();
    require(
        displayed.getCommittedComponentView(3)->getGridPosition() == GridCoords{0, 0} &&
            dynamic_cast<const FlipFlop*>(displayed.getLogicComponent(3)) &&
            displayed.getLogicComponent(3)->getStateOutPin(0) && !displayed.getLogicComponent(3)->getStateOutPin(1),
        "Authored ID gap was not mapped to the live flip-flop."
    );
    require(
        displayed.getCommittedComponentView(3)->getOutputPins()[0].state == PinState::ON &&
            displayed.getCommittedComponentView(3)->getOutputPins()[1].state == PinState::OFF,
        "Independent Q/~Q visual states were swapped."
    );
    require(
        std::any_of(
            displayed.getWires().begin(), displayed.getWires().end(),
            [](const auto& item) { return item.second.getState() == PinState::ON; }
        ),
        "Live internal wires were not powered."
    );
    openInfo(3);
    const auto info = ui.componentInfo(displayed);
    require(
        std::find(info.begin(), info.end(), "Q: 1 (ON)") != info.end(),
        "Popup showed authored initial state rather than live Q."
    );
    key(GLFW_KEY_ESCAPE);
    require(views.inspecting(), "Escape closed the viewer instead of its information popup.");
    cursor({-12, 2});
    for (auto mode : {EditorMode::Selection, EditorMode::Interaction})
    {
        input.setMode(mode);
        layout();
        mouse(GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS);
        mouse(GLFW_MOUSE_BUTTON_LEFT, GLFW_RELEASE);
        for (int code : {GLFW_KEY_1, GLFW_KEY_DELETE, GLFW_KEY_SPACE, GLFW_KEY_PERIOD})
            key(code);
        key(GLFW_KEY_Z, GLFW_MOD_CONTROL);
        key(GLFW_KEY_V, GLFW_MOD_CONTROL);
        require(
            input.isIdle() && circuitToJson(parent, views.name(0)) == saved && parent.clockTime() == time,
            "Read-only interaction, spawn/delete, history, paste or clock shortcut changed state."
        );
    }
    require(
        !EditorActions(views.displayedScene()).apply({ConfigureInput{1, false}}) &&
            !EditorActions(views.displayedScene()).beginMove(1),
        "Read-only action boundary accepted edits."
    );
    click(ui.tabBounds(UI::Tab::Native));
    require(!ui.buttons().empty(), "Native palette is unexpectedly empty.");
    click(ui.buttons().front().bounds);
    require(
        !ui.dragging() && !input.getUiCapture().pointer && !ui.takeOpenDraft(),
        "Read-only palette trapped input capture."
    );
    const auto fileOptions = ui.menuOptions(UI::Menu::File);
    for (std::size_t i = 0; i < ui.fileCommands().size(); ++i)
        require(!fileOptions[i].enabled, "Viewer offered saving or replacing the observed scene.");
    for (const auto label : {"Examples", "About / Licenses"})
    {
        const auto item = std::find_if(
            fileOptions.begin(), fileOptions.end(), [&](const auto& option) { return option.label == label; }
        );
        require(
            item != fileOptions.end() && item->enabled,
            "Viewer blocked a menu action that does not modify the observed scene."
        );
    }
    require(!ui.menuOptions(UI::Menu::Simulation)[0].enabled, "Viewer offered reset simulation.");
    click(ui.bottomTabBounds(UI::BottomTab::Clocks));
    click(ui.clockControlBounds(UI::ClockControl::SingleStep));
    require(parent.clockTime() > time && views.inspecting(), "Parent clock controls cannot step while observing.");
    parent.togglePauseAllClocks();
    parent.updateClocks(0.125f, &views.activeClockTimeline());
    parent.syncVisuals();
    views.refreshInspection(input);
    require(
        parent.clockTime() > time && displayed.getLogicComponent(6)->getStateOutPin(0),
        "Parent simulation did not continue, or internal clock state remained stale."
    );
    parent.handleClick(data);
    parent.propagate();
    views.refreshInspection(input);
    require(
        !displayed.getLogicComponent(1)->getStateOutPin(0) && displayed.getLogicComponent(3)->getStateOutPin(0),
        "Live data change was mistaken for a flip-flop edge."
    );
    parent.stepAllClocks(1, &views.activeClockTimeline());
    views.refreshInspection(input);
    require(!displayed.getLogicComponent(3)->getStateOutPin(0), "Viewer failed to observe the next sampled edge.");
    click(ui.inspectionBackBounds());
    require(
        !views.inspecting() && !input.isReadOnly() && input.getPanOffset() == glm::vec2(0.1f, 0.07f) &&
            input.getZoom() == 1.25f,
        "Back failed to restore the parent camera."
    );
    openInfo(second);
    click(ui.viewInsideBounds(parent));
    require(
        views.inspectionLabel() == "Register 2" &&
            !std::as_const(views).displayedScene().getLogicComponent(3)->getStateOutPin(0),
        "Second instance displayed the first instance's state."
    );
    key(GLFW_KEY_ESCAPE);
    require(!views.inspecting(), "Escape did not return from a viewer without a popup.");
    // Nested navigation observes the nested placement, not its library editor.
    Scene wrapper;
    edit(wrapper, {RegisterComponentDefinition{definition}});
    const int nested = wrapper.addComponent(definition.identity.id, {4, 0});
    const int source = wrapper.addComponent(BuiltinComponentIds::Input, {-12, 0});
    const int output = wrapper.addComponent(BuiltinComponentIds::Output, {16, 0});
    edit(
        wrapper, {ConfigureComponentProperties{.componentId = source, .label = "Data"},
                  ConfigureComponentProperties{.componentId = output, .label = "Q"},
                  AddWire{{{-11, 0}, {-4, 0}, {-4, 1}, {0, 1}}}, AddWire{{{8, 1}, {15, 1}, {15, 0}}}}
    );
    const auto wrapped = makeSubcircuit(wrapper, {"subcircuit.live-wrapper", 1}, "Wrapper");
    edit(parent, {RegisterComponentDefinition{wrapped}});
    const int placedWrapper = parent.addComponent(wrapped.identity.id, {0, -12});
    parent.propagate();
    require(views.inspectComponent(placedWrapper, input), "Cannot inspect wrapper.");
    layout();
    click(ui.tabBounds(UI::Tab::Custom));
    require(!ui.buttons().empty(), "Wrapper should contain a registered child design.");
    const auto row = ui.buttons().front().bounds;
    glfwSetCursorPos(window, row.x + 8, row.y + 8);
    Input::cursorPositionCallback(window, row.x + 8, row.y + 8);
    mouse(GLFW_MOUSE_BUTTON_RIGHT, GLFW_PRESS);
    mouse(GLFW_MOUSE_BUTTON_RIGHT, GLFW_RELEASE);
    require(
        !ui.takeEditSubcircuit() && !input.getUiCapture().pointer,
        "Read-only custom palette opened an editor or trapped pointer capture."
    );
    input.setPanOffset({0.3f, 0.4f});
    input.setZoom(1.6f);
    openInfo(nested);
    require(ui.editSubcircuitBounds(views.displayedScene()).width == 0, "Nested live popup offered editing.");
    click(ui.viewInsideBounds(views.displayedScene()));
    require(
        views.inspecting() && views.inspectionLabel() == "Wrapper 1 / Register 1",
        "Cannot navigate into a nested instance."
    );
    require(
        views.leaveInspection(input) && views.inspecting() && input.getPanOffset() == glm::vec2(0.3f, 0.4f) &&
            input.getZoom() == 1.6f,
        "Nested Back lost the containing view's camera."
    );
    // Parent edits replace runtime objects: dismiss before any borrowed pointer is read.
    edit(parent, {DeleteComponent{placedWrapper}});
    require(
        !views.refreshInspection(input) && !views.inspecting() && !input.isReadOnly(),
        "Deleting the observed instance left stale runtime references."
    );
    require(views.inspectComponent(first, input), "Cannot reopen live instance.");
    views.create(input);
    require(
        !views.inspecting() && !input.isReadOnly() && views.activeIndex() == 1,
        "Switching workspaces retained a live viewer."
    );
    input.setUiInputHandler({});
    input.setScene(nullptr);
    glfwSetWindowUserPointer(window, nullptr);
}
} // namespace


int main()
{
    glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_NULL);
    if (!glfwInit())
        return 1;
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    auto* window = glfwCreateWindow(800, 600, "Live subcircuit inspection", nullptr, nullptr);
    int result = 0;
    try
    {
        require(window != nullptr, "Cannot create headless window.");
        run(window);
        std::cout << "PASS: subcircuit_live_inspection\n";
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL: " << error.what() << '\n';
        result = 1;
    }
    if (window)
        glfwDestroyWindow(window);
    glfwTerminate();
    return result;
}
