#include "Components/Definitions/NativeDefinitions.h"
#include "Editor/Actions/EditorActions.h"
#include "Editor/CircuitViews.h"
#include "Editor/Clipboard.h"
#include "Editor/Gestures/Selection.h"
#include "Editor/Input.h"
#include "Editor/Scene.h"
#include "Editor/Subcircuits.h"
#include "Geometry/GridSystem.h"
#include "Graphics/Text/TextGeometry.h"
#include "UI/UI.h"

#include <GLFW/glfw3.h>
#include <iostream>
#include <stdexcept>
#include <string>

namespace
{
void require(bool value, const char* message)
{
    if (!value)
        throw std::runtime_error(message);
}


EditResult edit(Scene& scene, const EditBatch& batch)
{
    auto result = EditorActions(scene).apply(batch);
    if (!result)
        throw std::runtime_error(result.message);
    return result;
}


/** @brief Verify clipboard copies preserve component configuration, geometry and reusable definitions. */
void settings(GLFWwindow*)
{
    Scene source;
    const int gate = source.addComponent(BuiltinComponentIds::And, {0, 0});
    const int input = source.addComponent(BuiltinComponentIds::Input, {-12, 0}, {.inputState = true});
    const int clock = source.addComponent(BuiltinComponentIds::Clock, {12, 0});
    edit(
        source,
        {ConfigureComponentProperties{
             .componentId = gate,
             .label = "Parity",
             .inputCount = 8,
             .inverted = true,
             .labelVisible = false,
             .inputInversion = ConfigureGateInput{6, true},
             .orientation = Orientation::Down
         },
         ConfigureComponentProperties{.componentId = input, .label = "Enable"},
         ConfigureComponentProperties{
             .componentId = clock, .label = "Timing", .clockFrequency = 16.f, .clockPaused = true, .labelVisible = true
         }}
    );
    const auto latch =
        edit(
            source, {CreateComponent{
                        .definitionId = BuiltinComponentIds::DLatch, .position = {24, 0}, .orientation = Orientation::Up
                    }}
        ).createdComponentIds.front();
    Selection selection;
    const std::vector<int> ids{gate, input, clock, latch};
    selection.selectGroup(ids);
    EditorClipboard clipboard;
    require(clipboard.copy(source, selection), "Nonempty selection was not copied.");
    selection.clear();
    require(!clipboard.copy(source, selection), "Empty copy should keep the prior clipboard.");
    Scene destination;
    const auto pasted = edit(destination, clipboard.pasteBatch(destination, {0, 12}));
    require(pasted.createdComponentIds.size() == 4, "Paste lost a selected component.");
    for (std::size_t i = 0; i < ids.size(); ++i)
    {
        const auto* before = source.getCommittedComponentView(ids[i]);
        // Clipboard ordering is deterministic by selected ID, not insertion into selection.
        const int afterId = pasted.createdComponentIds[i];
        const auto* after = destination.getCommittedComponentView(afterId);
        require(
            after->getGridPosition() == GridCoords{before->getGridPosition().x - 6, 12} &&
                before->getBodyLabel() == after->getBodyLabel() &&
                before->showsBodyLabel() == after->showsBodyLabel() &&
                before->getOrientation() == after->getOrientation() && before->getSize() == after->getSize() &&
                before->getDefinitionIdentity().id == after->getDefinitionIdentity().id &&
                before->getDefinitionIdentity().version == after->getDefinitionIdentity().version &&
                before->getInputPins().size() == after->getInputPins().size(),
            "Paste changed the component label, layout, identity or relative placement."
        );
        for (std::size_t pin = 0; pin < before->getInputPins().size(); ++pin)
            require(
                before->getInputPins()[pin].relative_pos == after->getInputPins()[pin].relative_pos &&
                    before->getInputPins()[pin].inverted == after->getInputPins()[pin].inverted,

                "Paste moved a configured input pin."
            );
    }
    const auto revision = destination.getRevision();
    const auto centered = EditorActions(destination).apply(clipboard.pasteBatch(destination, {0, 12}));
    require(
        centered.error == EditError::Overlap && destination.getRevision() == revision &&
            destination.getComponentCount() == 4,
        "Direct clipboard creation accepted identical centers or partially committed."
    );
    const auto* copiedGate = dynamic_cast<const Gate*>(destination.getLogicComponent(pasted.createdComponentIds[0]));
    const auto* copiedInput =
        dynamic_cast<const InputPin*>(destination.getLogicComponent(pasted.createdComponentIds[1]));
    const auto* copiedClock = dynamic_cast<const Clock*>(destination.getLogicComponent(pasted.createdComponentIds[2]));
    require(
        copiedGate && copiedGate->getInputPinCount() == 8 && copiedGate->isInverted() &&
            copiedGate->isInputInverted(6) && !copiedGate->isInputInverted(0) && copiedInput &&
            copiedInput->getState() && copiedClock && copiedClock->isPaused() && copiedClock->getFrequency() == 16.f,
        "Paste lost arity, inversion, input state or clock settings."
    );

    Scene wireScene;
    auto added = edit(wireScene, {AddWire{{{-6, 0}, {0, 0}, {0, 6}}}});
    selection.selectWire(wireScene, added.insertedWireIds.front(), {-3, 0});
    clipboard.copy(wireScene, selection);
    const auto segment = clipboard.pasteBatch(destination, {0, -12});
    require(
        segment.size() == 1 && std::get<AddWire>(segment.front()).path == std::vector<GridCoords>{{-3, -12}, {3, -12}},
        "An ordinary wire selection copied more than its selected segment."
    );
    selection.clear();
    selection.toggleWire(added.insertedWireIds.front());
    clipboard.copy(wireScene, selection);
    const auto whole = clipboard.pasteBatch(destination, {0, -12});
    require(
        std::get<AddWire>(whole.front()).path == std::vector<GridCoords>{{-3, -15}, {3, -15}, {3, -9}},
        "Whole-wire copy lost its bends or relative geometry."
    );
}


struct Editor
{
    Scene scene;
    Input input;
    GLFWwindow* window;
    int first, second;


    explicit Editor(GLFWwindow* window) : window(window)
    {
        input.setScene(&scene);
        glfwSetWindowUserPointer(window, &input);
        first = scene.addComponent(BuiltinComponentIds::Not, {-5, 0});
        second = scene.addComponent(BuiltinComponentIds::Not, {5, 0});
        edit(scene, {AddWire{{{-4, 0}, {3, 0}}}, AddWire{{{-12, 8}, {12, 8}}}});
    }


    ~Editor() { glfwSetWindowUserPointer(window, nullptr); }


    void cursor(GridCoords grid)
    {
        const auto point = input.getCameraFrame(window).worldToWindow(GridSystem::gridToWorld(grid));
        require(point.has_value(), "Inactive test canvas.");
        glfwSetCursorPos(window, point->x, point->y);
        input.handleCursorPos(window, point->x, point->y);
    }


    void mouse(int action, int mods = 0) { input.handleMouseButton(window, GLFW_MOUSE_BUTTON_LEFT, action, mods); }


    void key(int code, int mods = GLFW_MOD_CONTROL)
    {
        input.handleKey(code, GLFW_PRESS, mods);
        input.process(window);
        input.handleKey(code, GLFW_RELEASE, mods);
    }


    void copyGroup()
    {
        cursor({-9, -4});
        mouse(GLFW_PRESS, GLFW_MOD_SHIFT);
        cursor({9, 4});
        mouse(GLFW_RELEASE, GLFW_MOD_SHIFT);
        key(GLFW_KEY_C);
    }
};


/** @brief Exercise keyboard copy/paste, selection, preview cancellation and undo/redo as editor transactions. */
void shortcuts(GLFWwindow* window)
{
    Editor editor(window);
    editor.copyGroup();
    require(
        editor.input.getUndoCount() == 0 && editor.scene.getComponentCount() == 2,
        "Copy mutated the circuit or history."
    );
    editor.cursor({0, -12});
    editor.key(GLFW_KEY_V, 0);
    require(editor.scene.getComponentCount() == 2, "Bare V pasted the clipboard.");
    const auto before = editor.scene.getRevision();
    const auto builds = editor.scene.getTopologyBuildCount();
    editor.key(GLFW_KEY_V);
    require(
        editor.input.getPastePreview().active() && !editor.input.isIdle() && editor.scene.getComponentCount() == 2 &&
            editor.input.getUndoCount() == 0,
        "Ctrl+V committed instead of starting a preview."
    );
    editor.cursor({0, 0});
    require(
        editor.input.getPastePreview().overlappingComponents().size() == 2,
        "Overlapping preview components lost their advisory highlight."
    );
    require(
        editor.scene.getRevision() == before && editor.scene.getTopologyBuildCount() == builds &&
            editor.input.getUndoCount() == 0,
        "Preview movement changed the real scene or history."
    );
    editor.cursor({0, -12});
    require(
        editor.input.getPastePreview().overlappingComponents().empty(),
        "Overlap warning did not clear at a free location."
    );
    const auto* preview = editor.input.getPastePreview().scene();
    require(
        preview->getComponentViewMap().begin()->second->getGridPosition().y == -12 &&
            preview->getVisibleWires().size() == 1,
        "Preview did not follow the cursor with its internal wires."
    );
    editor.mouse(GLFW_PRESS);
    editor.mouse(GLFW_RELEASE);
    require(!editor.input.getPastePreview().active(), "Successful placement retained its preview.");
    const auto selected = editor.input.getSelectedComponents();
    require(
        selected.size() == 2 && editor.input.getSelectedWires().size() == 1 && editor.scene.getComponentCount() == 4 &&
            editor.scene.wireCount() == 3 && editor.input.getUndoCount() == 1,
        "Group paste did not select its objects or did not create one undo step."
    );
    auto it = selected.begin();
    const int first = *it++, second = *it;
    require(
        editor.scene.getCommittedComponentView(first)->getGridPosition() == GridCoords{-5, -12} &&
            editor.scene.getCommittedComponentView(second)->getGridPosition() == GridCoords{5, -12} &&
            editor.scene.netOfPin({first, 0}, PinType::OUTPUT) == editor.scene.netOfPin({second, 0}, PinType::INPUT),
        "Pasted components lost relative positions or internal connectivity."
    );
    const auto revision = editor.scene.getRevision();
    editor.key(GLFW_KEY_V);
    require(
        editor.input.getPastePreview().overlappingComponents().size() == 2,
        "Preview missed overlap at the original paste location."
    );
    editor.input.handleMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT, GLFW_PRESS, 0);
    editor.input.handleMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT, GLFW_RELEASE, 0);
    require(
        !editor.input.getPastePreview().active() && editor.scene.getRevision() == revision &&
            editor.input.getUndoCount() == 1,
        "Right-click did not cancel without editing history."
    );
    editor.key(GLFW_KEY_Z);
    require(
        editor.scene.getComponentCount() == 2 && editor.scene.wireCount() == 2 && editor.input.getRedoCount() == 1,
        "Undo did not remove the whole paste."
    );
    editor.cursor({0, 0});
    editor.key(GLFW_KEY_V);
    editor.key(GLFW_KEY_ESCAPE, 0);
    require(
        editor.scene.getComponentCount() == 2 && editor.input.getRedoCount() == 1 &&
            !editor.input.getPastePreview().active(),
        "Cancelled overlapping preview erased redo or modified the circuit."
    );
    editor.key(GLFW_KEY_Y);
    require(
        editor.scene.getComponentCount() == 4 && editor.scene.wireCount() == 3, "Redo did not restore the whole paste."
    );
    editor.cursor({0, 16});
    editor.input.handleKey(GLFW_KEY_V, GLFW_REPEAT, GLFW_MOD_CONTROL);
    editor.input.process(window);
    require(editor.scene.getComponentCount() == 4, "Auto-repeat duplicated pasted objects.");

    editor.input.setMode(EditorMode::Interaction);
    editor.key(GLFW_KEY_V);
    editor.input.setMode(EditorMode::Selection);
    editor.input.process(window);
    require(editor.scene.getComponentCount() == 4, "Interaction mode queued a structural edit.");
    editor.input.setUiCapture({true, true});
    editor.key(GLFW_KEY_V);
    editor.input.setUiCapture({});
    editor.input.setCanvasFocused(true);
    editor.input.process(window);
    require(editor.scene.getComponentCount() == 4, "Captured paste leaked into the canvas.");
    editor.input.handleKey(GLFW_KEY_V, GLFW_PRESS, GLFW_MOD_CONTROL);
    editor.input.handleFocus(false);
    editor.input.handleFocus(true);
    editor.input.process(window);
    require(editor.scene.getComponentCount() == 4, "Focus loss retained a queued paste.");
    glfwSetCursorPos(window, -1, -1);
    editor.key(GLFW_KEY_V);
    editor.cursor({0, 16});
    editor.input.process(window);
    require(editor.scene.getComponentCount() == 4, "Outside-canvas paste fired on pointer reentry.");

    editor.input.setUiInputHandler([](const UiInputEvent& event)
                                   { return event.kind == UiInputKind::Key && event.code == GLFW_KEY_V; });
    editor.input.setCanvasFocused(true);
    editor.key(GLFW_KEY_V);
    editor.input.setUiInputHandler({});
    require(editor.scene.getComponentCount() == 4, "UI-consumed paste changed the circuit.");
    editor.cursor({-5, 0});
    editor.mouse(GLFW_PRESS);
    editor.cursor({-5, 2});
    editor.key(GLFW_KEY_C);
    editor.key(GLFW_KEY_V);
    editor.key(GLFW_KEY_ESCAPE, 0);
    editor.mouse(GLFW_RELEASE);
    require(
        editor.scene.getComponentCount() == 4 &&
            editor.scene.getCommittedComponentView(editor.first)->getGridPosition() == GridCoords{-5, 0},
        "Copy/paste during a drag committed preview geometry."
    );

    for (int cancellation = 0; cancellation < 5; ++cancellation)
    {
        editor.cursor({0, 16});
        editor.key(GLFW_KEY_V);
        require(editor.input.getPastePreview().active(), "Cancellation fixture did not start a preview.");
        if (cancellation == 0)
        {
            editor.input.setUiCapture({true, true});
            editor.input.setUiCapture({});
            editor.input.setCanvasFocused(true);
        }
        else if (cancellation == 1)
        {
            editor.input.handleFocus(false);
            editor.input.handleFocus(true);
        }
        else if (cancellation == 2)
        {
            editor.input.setMode(EditorMode::Interaction);
            editor.input.setMode(EditorMode::Selection);
        }
        else if (cancellation == 3)
        {
            glfwSetCursorPos(window, -1, -1);
            editor.input.handleCursorPos(window, -1, -1);
        }
        else
        {
            editor.key(GLFW_KEY_Z);
        }
        editor.mouse(GLFW_RELEASE);
        require(
            !editor.input.getPastePreview().active() && editor.scene.getComponentCount() == 4 &&
                editor.input.getUndoCount() == 1,
            "Cancellation committed or retained an unfinished preview."
        );
    }

    // A clipboard snapshot outlives its source objects and remains usable in another tab.
    Scene destination;
    editor.key(GLFW_KEY_V);
    editor.input.setScene(&destination);
    require(!editor.input.getPastePreview().active(), "Scene switch retained a paste preview.");
    editor.cursor({0, 0});
    editor.key(GLFW_KEY_V);
    editor.mouse(GLFW_PRESS);
    editor.mouse(GLFW_RELEASE);
    require(
        destination.getComponentCount() == 2 && destination.wireCount() == 1 && editor.input.getUndoCount() == 1,
        "Switching scenes lost the clipboard."
    );
    editor.key(GLFW_KEY_DELETE, 0);
    require(
        destination.getComponentCount() == 0 && destination.wireCount() == 0,
        "Delete did not delete exactly the pasted selection."
    );
    // Single pasted components can immediately be dragged without toggling them off.
    editor.input.setScene(&editor.scene);
    editor.cursor({-5, 0});
    editor.mouse(GLFW_PRESS);
    editor.mouse(GLFW_RELEASE);
    editor.key(GLFW_KEY_C);
    editor.input.setScene(&destination);
    editor.cursor({0, 0});
    editor.key(GLFW_KEY_V);
    editor.mouse(GLFW_PRESS);
    editor.mouse(GLFW_RELEASE);
    const int single = editor.input.getSelectedComponentId();
    editor.mouse(GLFW_PRESS);
    editor.cursor({0, 6});
    editor.mouse(GLFW_RELEASE);
    require(
        destination.getCommittedComponentView(single)->getGridPosition() == GridCoords{0, 6},
        "A pasted single component could not be dragged immediately."
    );
}


/** @brief Verify paste placement resolves anchor conflicts without corrupting existing scene objects. */
void overlappingPlacement(GLFWwindow* window)
{
    Editor editor(window);
    editor.copyGroup();
    editor.cursor({0, 0});
    editor.key(GLFW_KEY_V);
    const auto revision = editor.scene.getRevision();
    editor.mouse(GLFW_PRESS);
    editor.mouse(GLFW_RELEASE);
    require(
        editor.input.getPastePreview().active() && editor.input.getPastePreview().blockedComponents().size() == 2 &&
            editor.input.getLastEditError() == EditError::Overlap && editor.scene.getRevision() == revision &&
            editor.scene.getComponentCount() == 2 && editor.input.getUndoCount() == 0,
        "An exact-center collision committed, changed history or dismissed the preview."
    );
    editor.cursor({3, 0}); // Copies have different centers, but inputs land on original outputs.
    require(
        editor.input.getPastePreview().blockedComponents().size() == 2,
        "Coincident pins did not block the paste preview."
    );
    editor.mouse(GLFW_PRESS);
    editor.mouse(GLFW_RELEASE);
    require(
        editor.input.getPastePreview().active() && editor.input.getLastEditError() == EditError::Overlap &&
            editor.scene.getRevision() == revision && editor.input.getUndoCount() == 0,
        "A shared-pin paste committed or dismissed the preview."
    );
    editor.cursor({1, 0}); // Only bodies overlap here; all centers and pins are distinct.
    require(
        editor.input.getPastePreview().blockedComponents().empty(),
        "Moving to distinct centers and pins did not unblock placement."
    );
    require(
        editor.input.getPastePreview().overlappingComponents().size() == 2, "Partial overlap lost its placement notice."
    );
    const auto builds = editor.scene.getTopologyBuildCount();
    editor.mouse(GLFW_PRESS);
    editor.mouse(GLFW_RELEASE);
    require(
        editor.input.getPastePreview().active() && editor.scene.getRevision() == revision &&
            editor.scene.getTopologyBuildCount() == builds && editor.input.getUndoCount() == 0 &&
            editor.input.getLastEditError() == EditError::InvalidWire,
        "A paste covering existing connected pins did not cancel atomically."
    );
    editor.cursor({1, 4});
    editor.mouse(GLFW_PRESS);
    editor.mouse(GLFW_RELEASE);
    require(
        !editor.input.getPastePreview().active() && editor.scene.getComponentCount() == 4 &&
            editor.input.getSelectedComponents().size() == 2 && editor.input.getUndoCount() == 1 &&
            editor.scene.getTopologyBuildCount() == builds + 1 && editor.input.getLastEditError() == EditError::None,
        "Retrying paste in a clear area failed or created extra undo entries."
    );
    auto selected = editor.input.getSelectedComponents().begin();
    require(
        editor.scene.getCommittedComponentView(*selected++)->getGridPosition() == GridCoords{-4, 4} &&
            editor.scene.getCommittedComponentView(*selected)->getGridPosition() == GridCoords{6, 4},
        "Retried paste did not respect the chosen position."
    );
    editor.scene.propagate();
    bool shorted = false;
    for (const auto& [id, net] : editor.scene.getNets())
        shorted = shorted || net.shorted();
    require(!shorted, "Retried paste accidentally connected to the original wires.");
    editor.key(GLFW_KEY_Z);
    require(
        editor.scene.getComponentCount() == 2 && editor.scene.wireCount() == 2 && editor.input.getRedoCount() == 1,
        "Undo did not remove the retried paste as one edit."
    );
    editor.key(GLFW_KEY_Y);
    require(editor.scene.getComponentCount() == 4, "Redo rejected the previously accepted paste.");
}


/** @brief Verify UI ownership and editor mode route clipboard commands to the intended workspace. */
void appRouting(GLFWwindow* window)
{
    Input input;
    CircuitViews circuits;
    UI ui;
    FontMetrics font;
    circuits.select(0, input);
    ui.setCircuitViews(&circuits, &font);
    auto& scene = circuits.activeScene();
    const int original = scene.addComponent(BuiltinComponentIds::And, {0, 0});
    glfwSetWindowUserPointer(window, &input);
    auto layout = [&]() { ui.layout(scene.getComponentCatalog(), input.getCameraFrame(window).surface, input); };
    layout();
    // Match Engine's UI-first dispatch, including layout before and after every event.
    input.setUiInputHandler(
        [&](const UiInputEvent& event)
        {
            layout();
            ui.update(glfwGetTime(), input);
            const bool consumed = ui.handleInput(event, scene, input, input.getCameraFrame(window));
            layout();
            return consumed;
        }
    );
    auto cursor = [&](GridCoords position)
    {
        const auto point = input.getCameraFrame(window).worldToWindow(GridSystem::gridToWorld(position));
        require(point.has_value(), "App canvas is inactive.");
        glfwSetCursorPos(window, point->x, point->y);
        Input::cursorPositionCallback(window, point->x, point->y);
    };
    auto key = [&](int code, int action, int mods) { Input::keyCallback(window, code, 0, action, mods); };
    cursor({0, 0});
    Input::mouseButtonCallback(window, GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS, 0);
    Input::mouseButtonCallback(window, GLFW_MOUSE_BUTTON_LEFT, GLFW_RELEASE, 0);
    require(input.getSelectedComponents() == std::set<int>{original}, "App UI blocked component selection.");
    key(GLFW_KEY_LEFT_CONTROL, GLFW_PRESS, GLFW_MOD_CONTROL);
    key(GLFW_KEY_C, GLFW_PRESS, GLFW_MOD_CONTROL);
    key(GLFW_KEY_C, GLFW_RELEASE, GLFW_MOD_CONTROL);
    cursor({8, 0});
    key(GLFW_KEY_V, GLFW_PRESS, GLFW_MOD_CONTROL);
    key(GLFW_KEY_V, GLFW_RELEASE, GLFW_MOD_CONTROL);
    key(GLFW_KEY_LEFT_CONTROL, GLFW_RELEASE, 0);
    // OS events may all arrive before the next application frame.
    layout();
    input.process(window);
    ui.update(glfwGetTime(), input);
    require(
        input.getPastePreview().active() && scene.getComponentCount() == 1,
        "App routing committed the pending paste before a click."
    );
    Input::mouseButtonCallback(window, GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS, 0);
    Input::mouseButtonCallback(window, GLFW_MOUSE_BUTTON_LEFT, GLFW_RELEASE, 0);
    require(
        scene.getComponentCount() == 2 && input.getSelectedComponents().size() == 1 &&
            scene.getCommittedComponentView(input.getSelectedComponentId())->getGridPosition() == GridCoords{8, 0} &&
            input.getUndoCount() == 1,
        "Ctrl+C/Ctrl+V failed through the application's actual UI routing."
    );
    input.setUiInputHandler({});
    ui.setCircuitViews(nullptr, nullptr);
    glfwSetWindowUserPointer(window, nullptr);
}


/** @brief Verify clipboard operations remain independent of workspace selection and read-only inspection. */
void views(GLFWwindow*)
{
    Scene authored;
    const int in = authored.addComponent(BuiltinComponentIds::Input, {-8, 0});
    const int out = authored.addComponent(BuiltinComponentIds::Output, {8, 0});
    edit(
        authored, {ConfigureComponentProperties{.componentId = in, .label = "Data"},
                   ConfigureComponentProperties{.componentId = out, .label = "Result"}, AddWire{{{-7, 0}, {7, 0}}}}
    );
    auto definition = makeSubcircuit(authored, {"subcircuit.clipboard", 1}, "Example");
    Scene source;
    const auto created = edit(
        source, {RegisterComponentDefinition{definition},
                 CreateComponent{definition.identity.id, {0, 0}, {}, PlacementPolicy::RejectOverlap, 1, "Block"}}
    );
    Selection selection;
    selection.selectComponent(created.createdComponentIds.front());
    EditorClipboard clipboard;
    clipboard.copy(source, selection);
    edit(source, {DeleteComponent{created.createdComponentIds.front()}});
    Scene destination;
    const auto pasted = edit(destination, clipboard.pasteBatch(destination, {0, 0}));
    const int a = pasted.createdComponentIds.front();
    const auto again = edit(destination, clipboard.pasteBatch(destination, {12, 0}));
    const int b = again.createdComponentIds.front();
    const int manual = destination.addComponent(BuiltinComponentIds::Input, {-6, 0}, {.inputState = true});
    const auto anchor = destination.getCommittedComponentView(a)->getInputPins().front().relative_pos;
    edit(destination, {AddWire{{{-5, 0}, anchor}}});
    destination.propagate();
    require(
        destination.getComponentCatalog().find(definition.identity.id) &&
            destination.getCommittedComponentView(a)->getBodyLabel() == "Block" &&
            destination.getLogicComponent(a)->getStateOutPin(0) &&
            !destination.getLogicComponent(b)->getStateOutPin(0) &&
            destination.getLogicComponent(manual)->getStateOutPin(0),
        "Cross-scene custom paste lost its definition or shared runtime between instances."
    );
    definition.identity.version = 2;
    edit(destination, {RegisterComponentDefinition{definition, true}});
    bool rejected = false;
    try
    {
        clipboard.pasteBatch(destination, {24, 0});
    }
    catch (const std::invalid_argument&)
    {
        rejected = true;
    }
    require(rejected, "A changed custom definition silently reinterpreted the clipboard.");

    Scene workspace;
    const int unnamed = workspace.addComponent(BuiltinComponentIds::Input, {0, 0});
    selection.selectComponent(unnamed);
    clipboard.copy(workspace, selection);
    Scene subcircuit;
    subcircuit.setInterfaceNamingRequired(true);
    const auto named = edit(subcircuit, clipboard.pasteBatch(subcircuit, {0, 0}));
    require(
        !subcircuit.getCommittedComponentView(named.createdComponentIds.front())->getBodyLabel().empty(),
        "An unnamed workspace port pasted into a subcircuit kept an invalid blank name."
    );
}
} // namespace


int main(int argc, char** argv)
{
    glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_NULL);
    if (!glfwInit())
        return 1;
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    auto* window = glfwCreateWindow(800, 800, "Clipboard tests", nullptr, nullptr);
    int status = 0;
    try
    {
        require(window != nullptr, "Cannot create test window.");
        const std::pair<const char*, void (*)(GLFWwindow*)> tests[] = {
            {"clipboard_settings", settings},
            {"clipboard_shortcuts", shortcuts},
            {"clipboard_views", views},
            {"clipboard_app_routing", appRouting},
            {"clipboard_overlapping_placement", overlappingPlacement}
        };
        for (const auto& [name, run] : tests)
            if (argc == 1 || std::string(argv[1]) == name)
            {
                run(window);
                std::cout << "PASS: " << name << '\n';
            }
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        status = 1;
    }
    if (window)
        glfwDestroyWindow(window);
    glfwTerminate();
    return status;
}
