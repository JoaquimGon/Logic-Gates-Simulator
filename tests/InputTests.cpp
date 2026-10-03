#include "Components/Definitions/NativeDefinitions.h"
#include "Editor/Actions/EditorActions.h"
#include "Editor/Input.h"
#include "Editor/Scene.h"
#include "Geometry/GridSystem.h"
#include "UI/UI.h"

#include <GLFW/glfw3.h>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace
{
void require(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}

class Editor
{
  public:
    explicit Editor(GLFWwindow* window) : window(window)
    {
        input.setScene(&scene);
        glfwSetWindowUserPointer(window, &input);
    }

    ~Editor() { glfwSetWindowUserPointer(window, nullptr); }

    void cursorPixels(double x, double y)
    {
        glfwSetCursorPos(window, x, y);
        Input::cursorPositionCallback(window, x, y);
    }

    void cursor(GridCoords position)
    {
        const auto point =
            input.getCameraFrame(window).worldToWindow(GridSystem::gridToWorld(position));
        require(point.has_value(), "Cursor helper received an inactive canvas.");
        cursorPixels(point->x, point->y);
    }

    void mouse(int button, int action) { Input::mouseButtonCallback(window, button, action, 0); }

    void key(int key)
    {
        Input::keyCallback(window, key, 0, GLFW_PRESS, 0);
        input.process(window);
        Input::keyCallback(window, key, 0, GLFW_RELEASE, 0);
    }

    int inverter(GridCoords position)
    {
        return scene.addGate(
            NOT,
            position,
            {0.2f, 0.1f},
            "NOTgate",
            {{PinType::INPUT, 0, PinState::DISCONNECTED, {-2, 0}}},
            {{PinType::OUTPUT, 0, PinState::DISCONNECTED, {1, 0}}}
        );
    }

    void wire(std::vector<GridCoords> path)
    {
        Wire wire;
        wire.setPath(path);
        scene.commitWire(std::move(wire));
    }

    void verifyPinLocations()
    {
        for (const auto& [id, view] : scene.getComponentViewMap())
        {
            for (PinType type : {PinType::INPUT, PinType::OUTPUT})
            {
                const auto& pins =
                    type == PinType::INPUT ? view->getInputPins() : view->getOutputPins();
                for (const auto& pin : pins)
                {
                    const GridCoords position = view->getAbsolutePinGridPos(pin);
                    NetId expected = INVALID_NET_ID;
                    for (const auto& [wireId, wire] : scene.getWires())
                    {
                        if (wire.containsPoint(position))
                        {
                            require(
                                expected == INVALID_NET_ID || expected == wire.getNet(),
                                "Pin lies on inconsistent wire nets."
                            );
                            expected = wire.getNet();
                        }
                    }
                    require(
                        scene.netOfPin({id, static_cast<int>(pin.pin_index)}, type) == expected,
                        "Pin connectivity still refers to its previous position."
                    );
                }
            }
        }
    }

    Scene scene;
    Input input;

  private:
    GLFWwindow* window;
};

void dragCancellation(GLFWwindow* window)
{
    for (int cancelMethod = 0; cancelMethod < 4; ++cancelMethod)
    {
        Editor editor(window);
        editor.scene.addInputPin({-8, 0}, {0.15f, 0.15f}, "inputPin", true);
        const int sink = editor.inverter({0, 0});
        editor.wire({{-7, 0}, {-2, 0}});
        editor.scene.propagate();
        const auto wireIds = editor.scene.getWireIds();
        const auto builds = editor.scene.getTopologyBuildCount();

        editor.cursor({0, 0});
        editor.mouse(GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS);
        require(!editor.input.isIdle(), "Component body did not start dragging.");
        editor.cursor({6, 4});
        require(
            editor.scene.getComponentView(sink)->getGridPosition() == GridCoords{6, 4},
            "Drag did not preview the new position."
        );

        editor.mouse(GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS);
        require(!editor.input.isIdle(), "Repeated mouse press abandoned the owned preview.");
        require(
            editor.scene.getCommittedComponentView(sink)->getGridPosition() == GridCoords{0, 0},
            "Drag preview changed model placement."
        );
        if (cancelMethod == 0)
            editor.key(GLFW_KEY_ESCAPE);
        else if (cancelMethod == 1)
            editor.mouse(GLFW_MOUSE_BUTTON_RIGHT, GLFW_PRESS);
        else if (cancelMethod == 2)
            editor.input.cancelCurrentAction();
        else
            editor.input.setScene(nullptr);

        require(editor.scene.getTopologyBuildCount() == builds, "Cancellation rebuilt topology.");
        require(editor.input.isIdle(), "Cancellation left an active gesture.");
        require(
            editor.scene.getComponentView(sink)->getGridPosition() == GridCoords{0, 0},
            "Cancelled drag did not restore its starting position."
        );
        require(editor.input.getSelectedComponentId() == -1, "Cancellation retained selection.");
        editor.verifyPinLocations();
        editor.scene.propagate();
        editor.scene.syncVisuals();
        require(
            editor.scene.getLogicComponent(sink)->getStateInPin(0), "Cancellation lost wiring."
        );
        require(
            editor.scene.getWireIds() == wireIds, "Cancellation changed settled wire geometry."
        );

        editor.mouse(GLFW_MOUSE_BUTTON_LEFT, GLFW_RELEASE);
        require(
            editor.scene.getComponentView(sink)->getGridPosition() == GridCoords{0, 0},
            "Mouse release committed a cancelled drag."
        );
    }
}

void dragCommit(GLFWwindow* window)
{
    for (GridCoords target : {GridCoords{6, 4}, GridCoords{0, 4}, GridCoords{12, 0}})
    {
        Editor editor(window);
        editor.scene.addInputPin({-8, 0}, {0.15f, 0.15f}, "inputPin", true);
        const int sink = editor.inverter({0, 0});
        editor.scene.addClock({12, 0}, {0.15f, 0.15f}, "clock");
        editor.wire({{-7, 0}, {-2, 0}});
        editor.wire({{-7, 0}, {-7, 4}, {-2, 4}});
        editor.scene.propagate();

        editor.cursor({0, 0});
        editor.mouse(GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS);
        const auto builds = editor.scene.getTopologyBuildCount();
        editor.cursor(target);
        editor.mouse(GLFW_MOUSE_BUTTON_LEFT, GLFW_RELEASE);

        require(
            editor.scene.getTopologyBuildCount() == builds + (target == GridCoords{12, 0} ? 0 : 1),
            "Drop rebuilt topology for rollback or more than once for commit."
        );
        const GridCoords expected = target == GridCoords{12, 0} ? GridCoords{0, 0} : target;
        require(editor.input.isIdle(), "Mouse release left an active drag.");
        require(
            editor.scene.getComponentView(sink)->getGridPosition() == expected,
            "Drag commit or overlap rollback chose the wrong position."
        );
        editor.verifyPinLocations();
        editor.scene.propagate();
        editor.scene.syncVisuals();
        const bool connected = target != GridCoords{6, 4};
        require(
            editor.scene.getLogicComponent(sink)->getStateInPin(0) == connected,
            "Committed drag did not reconnect or disconnect its input."
        );
        require(
            editor.scene.getLogicComponent(sink)->getStateOutPin() == !connected,
            "Committed drag propagated a stale signal."
        );
    }
}

void spawnPlacement(GLFWwindow* window)
{
    for (int key :
         {GLFW_KEY_1,
          GLFW_KEY_2,
          GLFW_KEY_3,
          GLFW_KEY_4,
          GLFW_KEY_5,
          GLFW_KEY_6,
          GLFW_KEY_7,
          GLFW_KEY_8,
          GLFW_KEY_9,
          GLFW_KEY_U,
          GLFW_KEY_I})
    {
        Editor editor(window);
        editor.scene.addClock({0, 0}, {0.15f, 0.15f}, "clock");
        const bool latch = key == GLFW_KEY_U || key == GLFW_KEY_I;
        const bool inverted = key == GLFW_KEY_4 || key == GLFW_KEY_6 || key == GLFW_KEY_8;
        const int outputX =
            latch || inverted ? 3 : (key >= GLFW_KEY_3 && key <= GLFW_KEY_8 ? 2 : 1);
        const int outputY = latch ? 1 : 0;
        const int shift = key == GLFW_KEY_1 || key == GLFW_KEY_2 || key == GLFW_KEY_9 ? 3 : 4;
        const int sink = editor.inverter({outputX + shift + 7, outputY - shift});
        editor.wire({{outputX, outputY}, {outputX + 5, outputY}});
        editor.wire({{outputX + shift, outputY - shift}, {outputX + shift + 5, outputY - shift}});

        editor.cursor({0, 0});
        const auto builds = editor.scene.getTopologyBuildCount();
        editor.key(key);
        require(
            editor.scene.getTopologyBuildCount() == builds + 1, "Spawn rebuilt before placement."
        );
        require(editor.scene.getComponentCount() == 3, "Spawn shortcut did not add one component.");

        int spawned = -1;
        for (const auto& [id, view] : editor.scene.getComponentViewMap())
        {
            if (id != 0 && id != sink)
                spawned = id;
        }
        require(spawned != -1, "Spawned component was not found.");
        require(
            editor.scene.getComponentView(spawned)->getGridPosition() == GridCoords{shift, -shift},
            "Spawn did not resolve the occupied position."
        );
        require(!editor.scene.checkOverlap(spawned), "Spawn left an overlapping component.");
        const std::string expectedDefinition =
            latch
                ? builtinDefinitionId(key == GLFW_KEY_U ? LatchType::SR_LATCH : LatchType::D_LATCH)
            : key == GLFW_KEY_1 ? BuiltinComponentIds::Input
            : key == GLFW_KEY_9 ? BuiltinComponentIds::Clock
                                : builtinDefinitionId(static_cast<GateType>(key - GLFW_KEY_2));
        const auto* declaration = editor.scene.getComponentCatalog().find(expectedDefinition);
        const auto* view = editor.scene.getCommittedComponentView(spawned);
        require(
            declaration && view->getDefinitionIdentity().id == expectedDefinition &&
                view->getSize() == glm::vec2(declaration->layout.width, declaration->layout.height),
            "Shortcut bypassed catalog identity or default sizing."
        );
        editor.verifyPinLocations();

        const auto& connections = editor.scene.getLogicComponent(sink)->getInConnections();
        require(connections.size() == 1, "Final spawn position did not attach to the sink.");
        require(
            connections[0].srcComponentId == spawned && connections[0].srcPinIndex == 0,
            "Spawn connected the sink to the wrong driver."
        );
        editor.scene.propagate();
        editor.scene.syncVisuals();
        require(
            editor.scene.getLogicComponent(sink)->getStateInPin(0) ==
                editor.scene.getLogicComponent(spawned)->getStateOutPin(0),
            "Spawn propagation used stale connectivity."
        );
    }
}

void wireSegmentDeletion(GLFWwindow* window)
{
    Editor editor(window);
    editor.wire({{0, 0}, {4, 0}, {4, 4}, {8, 4}});
    const auto builds = editor.scene.getTopologyBuildCount();
    editor.cursor({4, 2});
    editor.mouse(GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS);
    editor.mouse(GLFW_MOUSE_BUTTON_LEFT, GLFW_RELEASE);
    require(editor.input.hasSelectedSegment(), "Wire segment selection failed.");
    editor.key(GLFW_KEY_DELETE);
    require(
        editor.scene.getTopologyBuildCount() == builds + 1 && editor.scene.wireCount() == 2 &&
            editor.scene.netCount() == 2,
        "Keyboard middle-segment deletion rebuilt multiple times or lost remainders."
    );
    require(!editor.input.hasSelectedSegment(), "Deletion retained a stale selected segment.");
}

void interactionModes(GLFWwindow* window)
{
    Editor editor(window);
    const int source = editor.scene.addInputPin({-8, 0}, {0.15f, 0.15f}, "inputPin", true);
    const int gate = editor.inverter({12, 0});
    const int clock = editor.scene.addClock({18, 0}, {0.15f, 0.15f}, "clock");
    editor.wire({{-7, 0}, {10, 0}});
    editor.scene.propagate();
    const auto builds = editor.scene.getTopologyBuildCount();
    require(editor.input.getMode() == EditorMode::Selection, "Default mode is not Selection.");

    editor.cursor({-8, 0});
    editor.mouse(GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS);
    editor.cursor({-8, 4});
    require(
        editor.scene.getLogicComponent(source)->getStateOutPin(),
        "Selection mode toggled the input while dragging."
    );
    editor.mouse(GLFW_MOUSE_BUTTON_LEFT, GLFW_RELEASE);
    require(
        editor.scene.getCommittedComponentView(source)->getGridPosition() == GridCoords{-8, 4} &&
            editor.scene.getTopologyBuildCount() == builds + 1,
        "Input body did not commit a move in Selection mode."
    );
    editor.verifyPinLocations();
    editor.scene.propagate();
    require(
        !editor.scene.getLogicComponent(gate)->getStateInPin(0) &&
            editor.scene.getLogicComponent(source)->getStateOutPin(),
        "Moving an input changed its value or left a stale attachment."
    );

    editor.key(GLFW_KEY_SPACE);
    editor.key(GLFW_KEY_PERIOD);
    require(
        !static_cast<Clock*>(editor.scene.getLogicComponent(clock))->isPaused() &&
            !editor.scene.getLogicComponent(clock)->getStateOutPin(),
        "Selection mode operated runtime clock controls."
    );

    editor.key(GLFW_KEY_F2);
    require(
        editor.input.getMode() == EditorMode::Interaction &&
            editor.input.getSelectedComponentId() == -1,
        "F2 did not switch and clear selection."
    );
    const auto runtimeBuilds = editor.scene.getTopologyBuildCount();
    editor.cursor({-8, 4});
    editor.mouse(GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS);
    editor.cursor({-8, 8});
    editor.mouse(GLFW_MOUSE_BUTTON_LEFT, GLFW_RELEASE);
    require(
        !editor.scene.getLogicComponent(source)->getStateOutPin() &&
            editor.scene.getCommittedComponentView(source)->getGridPosition() ==
                GridCoords{-8, 4} &&
            editor.input.isIdle(),
        "Interaction mode did not toggle without dragging."
    );
    for (GridCoords body : {GridCoords{12, 0}, GridCoords{18, 0}})
    {
        editor.cursor(body);
        editor.mouse(GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS);
        editor.cursor({body.x, body.y + 4});
        editor.mouse(GLFW_MOUSE_BUTTON_LEFT, GLFW_RELEASE);
    }
    require(
        editor.scene.getCommittedComponentView(gate)->getGridPosition() == GridCoords{12, 0} &&
            editor.scene.getCommittedComponentView(clock)->getGridPosition() == GridCoords{18, 0},
        "Interaction mode fell back to dragging a nonactionable component."
    );
    for (GridCoords start : {GridCoords{-7, 4}, GridCoords{0, 0}, GridCoords{0, 8}})
    {
        editor.cursor(start);
        editor.mouse(GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS);
        editor.cursor({start.x, start.y + 4});
        require(!editor.input.isCurrentlyDrawingWire(), "Interaction mode started wiring.");
        editor.mouse(GLFW_MOUSE_BUTTON_LEFT, GLFW_RELEASE);
    }
    editor.cursor({-8, 4});
    for (int key :
         {GLFW_KEY_1,
          GLFW_KEY_2,
          GLFW_KEY_3,
          GLFW_KEY_4,
          GLFW_KEY_5,
          GLFW_KEY_6,
          GLFW_KEY_7,
          GLFW_KEY_8,
          GLFW_KEY_9,
          GLFW_KEY_U,
          GLFW_KEY_I,
          GLFW_KEY_DELETE,
          GLFW_KEY_BACKSPACE})
        editor.key(key);
    require(
        editor.scene.getComponentCount() == 3 && editor.scene.wireCount() == 1 &&
            editor.scene.getTopologyBuildCount() == runtimeBuilds,
        "Interaction shortcuts modified circuit structure."
    );
    editor.key(GLFW_KEY_SPACE);
    editor.key(GLFW_KEY_PERIOD);
    require(
        static_cast<Clock*>(editor.scene.getLogicComponent(clock))->isPaused() &&
            editor.scene.getLogicComponent(clock)->getStateOutPin(),
        "Interaction clock controls did not operate."
    );
    editor.key(GLFW_KEY_F2);
    editor.input.process(window);
    require(editor.scene.getComponentCount() == 3, "Blocked shortcuts fired after mode switch.");
    editor.cursor({-8, 4});
    editor.key(GLFW_KEY_DELETE);
    require(!editor.scene.getLogicComponent(source), "Selection mode could not delete an input.");
}

void modeCancellation(GLFWwindow* window)
{
    Editor editor(window);
    const int source = editor.scene.addInputPin({0, 0}, {0.15f, 0.15f}, "inputPin", true);
    const auto builds = editor.scene.getTopologyBuildCount();
    editor.cursor({0, 0});
    editor.mouse(GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS);
    editor.cursor({6, 4});
    Input::keyCallback(window, GLFW_KEY_1, 0, GLFW_PRESS, 0);
    Input::keyCallback(window, GLFW_KEY_F2, 0, GLFW_PRESS, 0);
    require(
        editor.input.getMode() == EditorMode::Interaction && editor.input.isIdle() &&
            editor.scene.getCommittedComponentView(source)->getGridPosition() == GridCoords{0, 0} &&
            editor.scene.getTopologyBuildCount() == builds,
        "Mode change did not cancel the preview immediately."
    );
    Input::keyCallback(window, GLFW_KEY_F2, 0, GLFW_REPEAT, 0);
    Input::keyCallback(window, GLFW_KEY_F2, 0, GLFW_PRESS, 0);
    require(editor.input.getMode() == EditorMode::Interaction, "Held F2 repeatedly toggled mode.");
    Input::keyCallback(window, GLFW_KEY_F2, 0, GLFW_RELEASE, 0);
    Input::keyCallback(window, GLFW_KEY_1, 0, GLFW_RELEASE, 0);
    editor.mouse(GLFW_MOUSE_BUTTON_LEFT, GLFW_RELEASE);
    require(
        editor.scene.getLogicComponent(source)->getStateOutPin(),
        "Release after mode switch toggled the input."
    );
    editor.key(GLFW_KEY_F2);
    editor.input.process(window);
    require(editor.scene.getComponentCount() == 1, "Queued edit escaped mode cancellation.");

    Input::keyCallback(window, GLFW_KEY_F2, 0, GLFW_PRESS, GLFW_MOD_CONTROL);
    Input::keyCallback(window, GLFW_KEY_F2, 0, GLFW_RELEASE, GLFW_MOD_CONTROL);
    require(editor.input.getMode() == EditorMode::Selection, "Modified F2 toggled unexpectedly.");
    editor.cursor({8, 8});
    editor.mouse(GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS);
    editor.cursor({12, 10});
    require(editor.input.isCurrentlyDrawingWire(), "Wire preview did not start.");
    editor.key(GLFW_KEY_F2);
    editor.key(GLFW_KEY_F2);
    editor.mouse(GLFW_MOUSE_BUTTON_LEFT, GLFW_RELEASE);
    require(
        editor.scene.wireCount() == 0 && editor.input.isIdle() &&
            editor.scene.getTopologyBuildCount() == builds,
        "Mode switching committed a cancelled wire."
    );

    editor.cursor({0, 0});
    editor.mouse(GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS);
    editor.cursor({5, 5});
    Input::keyCallback(window, GLFW_KEY_DELETE, 0, GLFW_PRESS, 0);
    Input::focusCallback(window, GLFW_FALSE);
    editor.input.process(window);
    editor.mouse(GLFW_MOUSE_BUTTON_LEFT, GLFW_RELEASE);
    require(
        editor.scene.getLogicComponent(source) && editor.input.isIdle() &&
            editor.scene.getCommittedComponentView(source)->getGridPosition() == GridCoords{0, 0} &&
            editor.scene.getTopologyBuildCount() == builds,
        "Focus loss did not discard the gesture/keys."
    );
}

void wireAndPanGestures(GLFWwindow* window)
{
    Editor editor(window);
    editor.scene.addInputPin({-8, 0}, {0.15f, 0.15f}, "inputPin", true);
    const int sink = editor.inverter({8, 4});
    auto builds = editor.scene.getTopologyBuildCount();
    editor.cursor({-7, 0});
    editor.mouse(GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS);
    editor.cursor({6, 4});
    require(
        editor.input.getActiveWire().getPath() == std::vector<GridCoords>{{-7, 0}, {6, 0}, {6, 4}},
        "Wire bend chose the wrong initial axis."
    );
    editor.mouse(GLFW_MOUSE_BUTTON_LEFT, GLFW_RELEASE);
    require(
        editor.scene.getTopologyBuildCount() == ++builds && editor.scene.netCount() == 1,
        "Wire release did not commit once."
    );
    editor.scene.propagate();
    editor.scene.syncVisuals();
    require(
        editor.scene.getLogicComponent(sink)->getStateInPin(0), "Wire gesture did not connect."
    );

    editor.cursor({0, 0});
    editor.mouse(GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS);
    require(
        !editor.input.isCurrentlyDrawingWire() && editor.input.hasSelectedSegment(),
        "Wire click did not defer branching."
    );
    editor.cursor({0, -4});
    require(
        editor.input.isCurrentlyDrawingWire() && editor.input.getWireOriginPin().componentId == 0,
        "Branch did not retain driver identity."
    );
    editor.mouse(GLFW_MOUSE_BUTTON_LEFT, GLFW_RELEASE);
    require(
        editor.scene.getTopologyBuildCount() == ++builds && editor.scene.netCount() == 1,
        "Branch did not commit normalized connectivity once."
    );

    const auto wireIds = editor.scene.getWireIds();
    editor.cursor({-7, 0});
    editor.mouse(GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS);
    editor.cursor({-7, 4});
    editor.mouse(GLFW_MOUSE_BUTTON_RIGHT, GLFW_PRESS);
    editor.mouse(GLFW_MOUSE_BUTTON_LEFT, GLFW_RELEASE);
    editor.mouse(GLFW_MOUSE_BUTTON_RIGHT, GLFW_RELEASE);
    require(
        editor.scene.getWireIds() == wireIds && editor.scene.getTopologyBuildCount() == builds,
        "Right-click wire cancellation changed geometry."
    );
    editor.cursor({-7, 0});
    editor.mouse(GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS);
    editor.cursor({-7, 4});
    editor.key(GLFW_KEY_ESCAPE);
    editor.mouse(GLFW_MOUSE_BUTTON_LEFT, GLFW_RELEASE);
    require(editor.scene.getWireIds() == wireIds, "Escape committed a cancelled wire.");
    editor.cursor({-7, 0});
    editor.mouse(GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS);
    editor.cursor({-7, 0});
    editor.mouse(GLFW_MOUSE_BUTTON_LEFT, GLFW_RELEASE);
    require(editor.scene.getWireIds() == wireIds, "Click/release committed a zero-length wire.");

    for (EditorMode mode : {EditorMode::Selection, EditorMode::Interaction})
    {
        editor.input.setMode(mode);
        editor.input.setZoom(2);
        const auto offset = editor.input.getPanOffset();
        glfwSetCursorPos(window, 400, 400);
        editor.mouse(GLFW_MOUSE_BUTTON_MIDDLE, GLFW_PRESS);
        editor.mouse(GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS);
        glfwSetCursorPos(window, 440, 420);
        Input::cursorPositionCallback(window, 440, 420);
        const auto delta = editor.input.getPanOffset() - offset;
        require(
            std::abs(delta.x + 0.05f) < 0.00001f && std::abs(delta.y - 0.025f) < 0.00001f,
            "Panning did not respect zoom and viewport height."
        );
        editor.key(GLFW_KEY_F2);
        const auto finishedOffset = editor.input.getPanOffset();
        glfwSetCursorPos(window, 480, 440);
        Input::cursorPositionCallback(window, 480, 440);
        editor.mouse(GLFW_MOUSE_BUTTON_MIDDLE, GLFW_RELEASE);
        editor.mouse(GLFW_MOUSE_BUTTON_LEFT, GLFW_RELEASE);
        require(
            editor.input.isIdle() && editor.input.getPanOffset() == finishedOffset &&
                editor.scene.getWireIds() == wireIds,
            "Mode change left panning or editing active."
        );
    }
}

void uiInputRouting(GLFWwindow* window)
{
    Editor editor(window);
    const int source = editor.scene.addInputPin({0, 0}, {0.15f, 0.15f}, "inputPin", true);
    const int clock = editor.scene.addClock({10, 0}, {0.15f, 0.15f}, "clock");
    editor.cursor({0, 0});
    editor.mouse(GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS);
    editor.mouse(GLFW_MOUSE_BUTTON_LEFT, GLFW_RELEASE);
    require(editor.input.getSelectedComponentId() == source, "Selection fixture failed.");
    std::vector<UiInputEvent> events;
    bool consume = false;
    editor.input.setUiInputHandler(
        [&](const UiInputEvent& event)
        {
            events.push_back(event);
            return consume;
        }
    );
    Input::keyCallback(window, GLFW_KEY_1, 27, GLFW_PRESS, GLFW_MOD_SHIFT);
    editor.input.setUiCapture({true, true});
    Input::keyCallback(window, GLFW_KEY_1, 27, GLFW_RELEASE, GLFW_MOD_SHIFT);
    for (int key :
         {GLFW_KEY_1,
          GLFW_KEY_DELETE,
          GLFW_KEY_BACKSPACE,
          GLFW_KEY_ESCAPE,
          GLFW_KEY_F2,
          GLFW_KEY_F3})
        editor.key(key);
    Input::charCallback(window, 0x03A9);
    editor.mouse(GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS);
    editor.cursor({6, 4});
    Input::scrollCallback(window, 1.5, 2.0);
    editor.input.setUiCapture({});
    editor.input.setCanvasFocused(true);
    editor.mouse(GLFW_MOUSE_BUTTON_LEFT, GLFW_RELEASE);
    editor.input.process(window);
    require(
        editor.scene.getComponentCount() == 2 && editor.input.isIdle() &&
            editor.input.getSelectedComponentId() == source &&
            editor.scene.getLogicComponent(source)->getStateOutPin() &&
            editor.input.getMode() == EditorMode::Selection &&
            !editor.input.consumeKeyPress(GLFW_KEY_F3),
        "Captured events escaped into scene edits or replayed on release."
    );
    require(
        events.front().scanCode == 27 && events.front().modifiers == GLFW_MOD_SHIFT,
        "UI lost native key payload."
    );
    bool text = false, mouse = false, cursor = false, scroll = false;
    for (const auto& event : events)
    {
        text |= event.kind == UiInputKind::Text && event.codepoint == 0x03A9;
        mouse |= event.kind == UiInputKind::MouseButton;
        cursor |= event.kind == UiInputKind::Cursor;
        scroll |= event.kind == UiInputKind::Scroll && event.x == 1.5 && event.y == 2.0;
    }
    require(text && mouse && cursor && scroll, "UI missed an input category.");

    editor.input.setUiCapture({true, false});
    Input::keyCallback(window, GLFW_KEY_1, 0, GLFW_PRESS, 0);
    editor.input.setUiCapture({});
    editor.input.setCanvasFocused(true);
    Input::keyCallback(window, GLFW_KEY_1, 0, GLFW_REPEAT, 0);
    Input::keyCallback(window, GLFW_KEY_1, 0, GLFW_PRESS, 0);
    editor.input.process(window);
    require(editor.scene.getComponentCount() == 2, "Held UI key replayed when capture ended.");
    Input::keyCallback(window, GLFW_KEY_1, 0, GLFW_RELEASE, 0);
    editor.key(GLFW_KEY_1);
    require(editor.scene.getComponentCount() == 3, "Fresh canvas key press remained blocked.");

    consume = true;
    editor.key(GLFW_KEY_F2);
    require(
        editor.input.getMode() == EditorMode::Selection && !editor.input.isCanvasFocused(),
        "Consumed event reached the canvas without persistent capture."
    );
    consume = false;
    editor.input.setCanvasFocused(true);
    editor.key(GLFW_KEY_F2);
    editor.cursor({0, 0});
    editor.input.setUiCapture({false, true});
    const bool sourceValue = editor.scene.getLogicComponent(source)->getStateOutPin();
    editor.mouse(GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS);
    editor.mouse(GLFW_MOUSE_BUTTON_LEFT, GLFW_RELEASE);
    require(
        editor.scene.getLogicComponent(source)->getStateOutPin() == sourceValue,
        "UI pointer capture operated a runtime input."
    );
    editor.input.setCanvasFocused(true);
    editor.key(GLFW_KEY_SPACE);
    require(
        static_cast<Clock*>(editor.scene.getLogicComponent(clock))->isPaused(),
        "Pointer capture also blocked an uncaptured focused keyboard."
    );
    editor.key(GLFW_KEY_SPACE);
    editor.input.setUiCapture({true, false});
    editor.key(GLFW_KEY_SPACE);
    editor.key(GLFW_KEY_PERIOD);
    require(
        !static_cast<Clock*>(editor.scene.getLogicComponent(clock))->isPaused() &&
            !editor.scene.getLogicComponent(clock)->getStateOutPin(),
        "Typing operated the clocks."
    );
    editor.input.setUiCapture({});
    editor.key(GLFW_KEY_SPACE);
    require(
        !static_cast<Clock*>(editor.scene.getLogicComponent(clock))->isPaused(),
        "Releasing capture silently restored keyboard focus."
    );
    editor.cursor({0, 0});
    editor.mouse(GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS);
    editor.mouse(GLFW_MOUSE_BUTTON_LEFT, GLFW_RELEASE);
    editor.key(GLFW_KEY_SPACE);
    require(
        static_cast<Clock*>(editor.scene.getLogicComponent(clock))->isPaused(),
        "Canvas click did not restore keyboard focus."
    );
    Input::focusCallback(window, GLFW_FALSE);
    editor.key(GLFW_KEY_F2);
    Input::focusCallback(window, GLFW_TRUE);
    editor.key(GLFW_KEY_F2);
    require(
        editor.input.getMode() == EditorMode::Selection && events.back().kind == UiInputKind::Key,
        "Focus loss leaked input or focus gain failed to recover."
    );
    bool focus = false;
    for (const auto& event : events)
        focus |= event.kind == UiInputKind::WindowFocus;
    require(focus, "UI missed window focus callbacks.");
}

void uiGestureCancellation(GLFWwindow* window)
{
    // Exercise each ownership transfer against drag previews, wire branches, and panning.
    for (int gesture = 0; gesture < 3; ++gesture)
        for (int transfer = 0; transfer < 4; ++transfer)
        {
            Editor editor(window);
            const int component = editor.inverter({0, 0});
            editor.wire({{-8, -4}, {8, -4}});
            bool consumeText = false;
            editor.input.setUiInputHandler(
                [&](const UiInputEvent& event)
                { return consumeText && event.kind == UiInputKind::Text; }
            );
            editor.cursor(gesture == 0 ? GridCoords{0, 0} : GridCoords{0, -4});
            const int button = gesture == 2 ? GLFW_MOUSE_BUTTON_MIDDLE : GLFW_MOUSE_BUTTON_LEFT;
            editor.mouse(button, GLFW_PRESS);
            editor.cursor({6, 4});
            require(!editor.input.isIdle(), "Gesture fixture did not become active.");
            const auto revision = editor.scene.getRevision();
            const auto builds = editor.scene.getTopologyBuildCount();
            const auto wireIds = editor.scene.getWireIds();
            const auto offset = editor.input.getPanOffset();
            if (transfer == 0)
                editor.input.setUiCapture({false, true});
            else if (transfer == 1)
                editor.input.setUiCapture({true, false});
            else if (transfer == 2)
                editor.input.setCanvasFocused(false);
            else
            {
                consumeText = true;
                Input::charCallback(window, 'x');
            }
            require(
                editor.input.isIdle() &&
                    editor.scene.getComponentView(component)->getGridPosition() == GridCoords{0, 0},
                "UI ownership failed to cancel an unfinished gesture."
            );
            if (gesture == 0)
                require(
                    editor.input.getSelectedComponentId() == component,
                    "Inspector focus discarded the selected component."
                );
            editor.input.setUiInputHandler({});
            editor.input.setCanvasFocused(true);
            editor.cursor({10, 8});
            editor.mouse(button, GLFW_PRESS); // Still physically held; cannot restart.
            editor.mouse(button, GLFW_RELEASE);
            require(
                editor.input.isIdle() && editor.scene.getRevision() == revision &&
                    editor.scene.getTopologyBuildCount() == builds &&
                    editor.scene.getWireIds() == wireIds && editor.input.getPanOffset() == offset,
                "Cancelled input resumed or its release committed geometry."
            );
            editor.verifyPinLocations();
        }

    Editor editor(window);
    const int component = editor.inverter({0, 0});
    bool sawCancelled = false;
    bool consume = false;
    editor.input.setUiInputHandler(
        [&](const UiInputEvent& event)
        {
            if (!consume || event.kind != UiInputKind::MouseButton)
                return false;
            editor.input.setUiCapture({false, true});
            sawCancelled =
                editor.input.isIdle() &&
                editor.scene.getComponentView(component)->getGridPosition() == GridCoords{0, 0};
            return true;
        }
    );
    editor.cursor({0, 0});
    editor.mouse(GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS);
    editor.cursor({6, 4});
    consume = true;
    editor.mouse(GLFW_MOUSE_BUTTON_RIGHT, GLFW_PRESS);
    require(
        sawCancelled && editor.input.getSelectedComponentId() == component,
        "UI event handler did not precede canvas right-click cancellation."
    );
    editor.mouse(GLFW_MOUSE_BUTTON_LEFT, GLFW_RELEASE);
    editor.mouse(GLFW_MOUSE_BUTTON_RIGHT, GLFW_RELEASE);
}

void canvasInputBounds(GLFWwindow* window)
{
    Editor editor(window);
    const int component = editor.inverter({0, 0});
    const CanvasViewport bounds{300, 300, 200, 200};
    require(
        bounds.contains(300, 300) && bounds.contains(499, 499) && !bounds.contains(500, 400) &&
            !bounds.contains(400, 500),
        "Canvas boundary inclusion is inconsistent."
    );
    editor.input.setCanvasViewport(bounds);
    editor.cursorPixels(400, 400);
    editor.mouse(GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS);
    editor.cursorPixels(420, 420);
    const auto revision = editor.scene.getRevision();
    for (const auto& invalid :
         {CanvasViewport{0, 0, -1, 20},
          CanvasViewport{0, 0, 20, std::numeric_limits<double>::infinity()}})
    {
        bool rejected = false;
        try
        {
            editor.input.setCanvasViewport(invalid);
        }
        catch (const std::invalid_argument&)
        {
            rejected = true;
        }
        require(
            rejected && editor.input.getCanvasViewport() == bounds && !editor.input.isIdle(),
            "Invalid bounds mutated canvas ownership."
        );
    }
    editor.cursorPixels(500, 420);
    require(
        editor.input.isIdle() && editor.input.getHoveredComponentId() == -1 &&
            editor.scene.getComponentView(component)->getGridPosition() == GridCoords{0, 0},
        "Leaving canvas bounds did not discard preview and hover."
    );
    editor.cursorPixels(420, 420);
    editor.mouse(GLFW_MOUSE_BUTTON_LEFT, GLFW_RELEASE);
    require(editor.scene.getRevision() == revision, "Reentry committed a cancelled preview.");
    editor.cursorPixels(500, 400);
    editor.mouse(GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS);
    editor.mouse(GLFW_MOUSE_BUTTON_LEFT, GLFW_RELEASE);
    editor.key(GLFW_KEY_1);
    editor.key(GLFW_KEY_DELETE);
    editor.input.handleKey(GLFW_KEY_LEFT_CONTROL, GLFW_PRESS);
    Input::scrollCallback(window, 0, 1);
    editor.input.handleKey(GLFW_KEY_LEFT_CONTROL, GLFW_RELEASE);
    require(
        editor.scene.getComponentCount() == 1 && editor.input.getZoom() == 1 &&
            !editor.input.isCanvasFocused(),
        "Sidebar input modified the canvas."
    );
    editor.input.cancelCurrentAction(); // A second click on a selected component deselects it.
    editor.cursorPixels(400, 400);
    editor.mouse(GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS);
    require(!editor.input.isIdle(), "Layout-change fixture did not start dragging.");
    editor.input.setCanvasViewport(CanvasViewport{0, 0, 800, 800});
    require(
        editor.input.isIdle() && editor.input.getSelectedComponentId() == component,
        "Canvas layout change did not cancel while preserving inspector selection."
    );
    editor.mouse(GLFW_MOUSE_BUTTON_LEFT, GLFW_RELEASE);
    editor.input.handleKey(GLFW_KEY_LEFT_CONTROL, GLFW_PRESS);
    Input::scrollCallback(window, 0, 1);
    editor.input.handleKey(GLFW_KEY_LEFT_CONTROL, GLFW_RELEASE);
    require(editor.input.getZoom() > 1, "Eligible canvas scroll did not zoom.");
    editor.input.setCanvasViewport(CanvasViewport{0, 0, 0, 800});
    editor.cursorPixels(400, 400);
    require(!editor.input.isCanvasPointerAvailable(window), "Zero-size canvas accepted input.");
    editor.input.setCanvasViewport(std::nullopt);
    require(editor.input.isCanvasPointerAvailable(window), "Full-window fallback rejected input.");
    editor.cursorPixels(800, 400);
    require(
        !editor.input.isCanvasPointerAvailable(window), "Full-window right edge accepted input."
    );
}

void canvasCameraInteraction(GLFWwindow* window)
{
    Editor editor(window);
    editor.input.setCanvasViewport(CanvasViewport{120, 100, 560, 400});
    editor.input.setZoom(2);
    editor.input.setPanOffset({0.25f, 0.1f});
    const int source = editor.scene.addInputPin({0, 0}, {0.15f, 0.15f}, "inputPin", true);
    const int sink = editor.inverter({12, 2});
    editor.cursor({0, 0});
    editor.input.process(window);
    require(
        editor.input.getHoveredComponentId() == source,
        "Offset canvas did not hit the rendered component."
    );
    editor.mouse(GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS);
    editor.cursor({4, 2});
    editor.mouse(GLFW_MOUSE_BUTTON_LEFT, GLFW_RELEASE);
    require(
        editor.scene.getCommittedComponentView(source)->getGridPosition() == GridCoords{4, 2},
        "Panned/zoomed subcanvas drag used full-window coordinates."
    );
    editor.cursor({8, 4});
    editor.key(GLFW_KEY_1);
    bool spawned = false;
    for (const auto& [id, view] : editor.scene.getComponentViewMap())
        spawned |= id != source && id != sink && view->getGridPosition() == GridCoords{8, 4};
    require(spawned, "Subcanvas spawn shortcut used a different camera than picking.");
    editor.cursor({5, 2});
    editor.mouse(GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS);
    editor.cursor({10, 2});
    editor.mouse(GLFW_MOUSE_BUTTON_LEFT, GLFW_RELEASE);
    editor.scene.propagate();
    require(
        editor.scene.getLogicComponent(sink)->getStateInPin(0),
        "Subcanvas wire gesture missed the visible pin locations."
    );
    editor.cursorPixels(400, 300);
    const auto anchor = *editor.input.getCameraFrame(window).windowToWorld({400, 300});
    editor.mouse(GLFW_MOUSE_BUTTON_MIDDLE, GLFW_PRESS);
    editor.cursorPixels(428, 320);
    editor.mouse(GLFW_MOUSE_BUTTON_MIDDLE, GLFW_RELEASE);
    require(
        glm::length(*editor.input.getCameraFrame(window).windowToWorld({428, 320}) - anchor) <
            0.00001f,
        "Panning did not keep the grabbed world point under the cursor."
    );
    editor.cursor({4, 2});
    editor.mouse(GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS);
    editor.cursor({6, 6});
    const auto revision = editor.scene.getRevision();
    glfwSetWindowSize(window, 1000, 700);
    editor.input.process(window);
    editor.mouse(GLFW_MOUSE_BUTTON_LEFT, GLFW_RELEASE);
    require(
        editor.input.isIdle() && editor.scene.getRevision() == revision &&
            editor.input.getSelectedComponentId() == source &&
            editor.scene.getCommittedComponentView(source)->getGridPosition() == GridCoords{4, 2},
        "Surface resize committed or retained a stale gesture."
    );
    editor.input.setCanvasViewport(std::nullopt);
    const auto resized = editor.input.getCameraFrame(window);
    require(
        resized.viewport == CanvasViewport{0, 0, 1000, 700},
        "Full-window canvas did not follow a resize."
    );
    glfwSetWindowSize(window, 800, 800);
}

void componentPalette(GLFWwindow* window)
{
    glfwSetWindowSize(window, 800, 800);
    Editor editor(window);
    UI ui;
    auto layout = [&]
    {
        ui.layout(
            editor.scene.getComponentCatalog(),
            editor.input.getCameraFrame(window).surface,
            editor.input
        );
    };
    layout();
    editor.input.setUiInputHandler(
        [&](const UiInputEvent& event)
        {
            layout();
            return ui.handleInput(
                event, editor.scene, editor.input, editor.input.getCameraFrame(window)
            );
        }
    );
    require(
        editor.input.getCameraFrame(window).viewport == CanvasViewport{220, 0, 580, 800},
        "Palette did not reserve the left side of the canvas."
    );
    const auto buttons = ui.buttons();
    require(buttons.size() == 12, "Palette did not use the registered component catalog.");
    const std::vector<std::string> expected{
        BuiltinComponentIds::Input,
        BuiltinComponentIds::Output,
        BuiltinComponentIds::Clock,
        BuiltinComponentIds::Not,
        BuiltinComponentIds::And,
        BuiltinComponentIds::Nand,
        BuiltinComponentIds::Or,
        BuiltinComponentIds::Nor,
        BuiltinComponentIds::Xor,
        BuiltinComponentIds::Nxor,
        BuiltinComponentIds::SrLatch,
        BuiltinComponentIds::DLatch
    };
    for (std::size_t i = 0; i < expected.size(); ++i)
        require(buttons[i].definitionId == expected[i], "Native card order is incorrect.");
    require(
        buttons[0].bounds.y == buttons[1].bounds.y && buttons[0].bounds.x < buttons[1].bounds.x &&
            buttons[2].bounds.y > buttons[0].bounds.y,
        "Native cards did not use a two-column grid."
    );
    auto switchTab = [&](UI::Tab tab)
    {
        const auto bounds = ui.tabBounds(tab);
        editor.cursorPixels(bounds.x + bounds.width / 2, bounds.y + bounds.height / 2);
        editor.mouse(GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS);
        editor.mouse(GLFW_MOUSE_BUTTON_LEFT, GLFW_RELEASE);
        require(
            ui.activeTab() == tab && editor.input.isIdle() && !ui.dragging(),
            "Tab click leaked into a drag or canvas gesture."
        );
    };
    switchTab(UI::Tab::Custom);
    require(
        ui.buttons().empty() && editor.scene.getComponentCount() == 0,
        "Empty custom tab contained placeholder components."
    );
    switchTab(UI::Tab::Native);
    for (std::size_t i = 0; i < buttons.size(); ++i)
    {
        const auto button = buttons[i];
        const auto before = editor.scene.getComponentCount();
        editor.cursorPixels(button.bounds.x + 20, button.bounds.y + 15);
        editor.mouse(GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS);
        require(
            ui.dragging() && editor.input.isIdle() &&
                editor.input.getUiCapture() == UiInputCapture{true, true} &&
                editor.scene.getComponentCount() == before,
            "Palette press leaked into a canvas gesture or created too early."
        );
        editor.key(GLFW_KEY_A);
        const GridCoords target{-8 + static_cast<int>(i % 3) * 8, 8 - static_cast<int>(i / 3) * 8};
        editor.cursor(target);
        editor.mouse(GLFW_MOUSE_BUTTON_LEFT, GLFW_RELEASE);
        require(
            !ui.dragging() && editor.input.getUiCapture() == UiInputCapture{} &&
                editor.scene.getComponentCount() == before + 1 &&
                editor.scene.getCommittedComponentView(static_cast<int>(before))
                        ->getDefinitionIdentity()
                        .id == button.definitionId &&
                editor.scene.getCommittedComponentView(static_cast<int>(before))
                        ->getGridPosition() == target &&
                editor.input.isIdle(),
            "Palette drop used the wrong component/transform or leaked release to the canvas."
        );
    }
    auto start = [&]
    {
        const auto button = ui.buttons().front();
        editor.cursorPixels(button.bounds.x + 20, button.bounds.y + 15);
        editor.mouse(GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS);
        require(ui.dragging(), "Palette button did not begin dragging.");
    };
    const auto count = editor.scene.getComponentCount();
    const auto builds = editor.scene.getTopologyBuildCount();
    start();
    editor.cursor({-8, 8});
    editor.mouse(GLFW_MOUSE_BUTTON_LEFT, GLFW_RELEASE);
    require(
        editor.scene.getComponentCount() == count && !ui.message().empty() &&
            editor.scene.getTopologyBuildCount() == builds,
        "Overlapping drop partially created a component or hid its rejection."
    );
    start();
    editor.mouse(GLFW_MOUSE_BUTTON_LEFT, GLFW_RELEASE);
    require(
        editor.scene.getComponentCount() == count, "Click without dragging created a component."
    );
    for (int cancel = 0; cancel < 4; ++cancel)
    {
        start();
        if (cancel == 0)
            editor.key(GLFW_KEY_ESCAPE);
        else if (cancel == 1)
        {
            editor.mouse(GLFW_MOUSE_BUTTON_RIGHT, GLFW_PRESS);
            editor.mouse(GLFW_MOUSE_BUTTON_RIGHT, GLFW_RELEASE);
        }
        else if (cancel == 2)
        {
            editor.input.handleFocus(false);
            editor.input.handleFocus(true);
        }
        else
        {
            glfwSetWindowSize(window, 800, 420);
            layout();
        }
        editor.cursor({10, -8});
        editor.mouse(GLFW_MOUSE_BUTTON_LEFT, GLFW_RELEASE);
        require(
            !ui.dragging() && editor.scene.getComponentCount() == count &&
                editor.input.getUiCapture() == UiInputCapture{} && editor.input.isIdle(),
            "Cancelled palette drag created a component or retained capture."
        );
    }
    const auto zoom = editor.input.getZoom();
    editor.cursorPixels(30, 120);
    Input::scrollCallback(window, 0, -20);
    require(
        editor.input.getZoom() == zoom && ui.buttons().back().bounds.y < 350,
        "Palette scrolling zoomed the canvas or hid the last item."
    );
    const auto partial = std::find_if(
        ui.buttons().begin(),
        ui.buttons().end(),
        [](const auto& item)
        { return item.bounds.y < 112 && item.bounds.y + item.bounds.height > 112; }
    );
    require(partial != ui.buttons().end(), "Scroll fixture did not leave a partly visible card.");
    const double partialX = partial->bounds.x + 4;
    editor.cursorPixels(partialX, 116);
    editor.mouse(GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS);
    require(ui.dragging(), "Visible part of a scrolled card could not be dragged.");
    editor.key(GLFW_KEY_ESCAPE);
    editor.mouse(GLFW_MOUSE_BUTTON_LEFT, GLFW_RELEASE);
    editor.cursorPixels(partialX, 108);
    editor.mouse(GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS);
    require(!ui.dragging(), "Hidden card area above the list accepted a drag.");
    editor.mouse(GLFW_MOUSE_BUTTON_LEFT, GLFW_RELEASE);
    editor.input.setMode(EditorMode::Interaction);
    layout();
    const auto button = ui.buttons().back();
    editor.cursorPixels(button.bounds.x + 20, button.bounds.y + 15);
    editor.mouse(GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS);
    editor.mouse(GLFW_MOUSE_BUTTON_LEFT, GLFW_RELEASE);
    require(
        !ui.dragging() && editor.scene.getComponentCount() == count,
        "Interaction mode allowed structural palette edits."
    );
    switchTab(UI::Tab::Custom);
    require(ui.buttons().empty(), "Custom tab included native components.");
    switchTab(UI::Tab::Native);
    require(ui.buttons().front().bounds.y == 112, "Tab switch retained stale scroll position.");
    editor.key(GLFW_KEY_F2);
    require(
        editor.input.getMode() == EditorMode::Selection,
        "Palette focus prevented switching back to Selection mode."
    );
    auto custom = *editor.scene.getComponentCatalog().find(BuiltinComponentIds::DLatch);
    custom.identity = {"test.custom-memory", 1};
    custom.displayName = "Custom memory";
    custom.presentation.kind = PresentationKind::Box;
    custom.presentation.shader = boxShaderResources();
    require(
        static_cast<bool>(EditorActions(editor.scene).apply({RegisterComponentDefinition{custom}})),
        "Custom palette fixture could not be registered."
    );
    layout();
    require(ui.buttons().size() == 12, "Registered custom component leaked into native cards.");
    switchTab(UI::Tab::Custom);
    require(
        ui.buttons().size() == 1 && ui.buttons()[0].label == "Custom memory" &&
            ui.buttons()[0].inputCount == 2 && ui.buttons()[0].outputCount == 2,
        "Custom list omitted the registered name or indexed pin counts."
    );
    start();
    editor.cursor({16, 8});
    editor.mouse(GLFW_MOUSE_BUTTON_LEFT, GLFW_RELEASE);
    require(
        editor.scene.getComponentCount() == count + 1 && !ui.dragging(),
        "Custom list entry did not retain drag-to-place behavior."
    );
    ui.cancel(editor.input);
    editor.input.setUiInputHandler({});
    glfwSetWindowSize(window, 800, 800);
}

void componentInformation(GLFWwindow* window)
{
    glfwSetWindowSize(window, 800, 600);
    Editor editor(window);
    UI ui;
    auto layout = [&]
    {
        ui.layout(
            editor.scene.getComponentCatalog(),
            editor.input.getCameraFrame(window).surface,
            editor.input
        );
    };
    layout();
    editor.input.setUiInputHandler(
        [&](const UiInputEvent& event)
        {
            layout();
            return ui.handleInput(
                event, editor.scene, editor.input, editor.input.getCameraFrame(window)
            );
        }
    );
    const int gate = editor.scene.addComponent(BuiltinComponentIds::And, {-6, 4});
    const int latch = editor.scene.addComponent(BuiltinComponentIds::SrLatch, {4, 4});
    const int source = editor.scene.addComponent(BuiltinComponentIds::Input, {-6, -6});
    const int clock = editor.scene.addComponent(BuiltinComponentIds::Clock, {4, -6});
    auto refresh = [&]
    {
        editor.scene.propagate();
        editor.scene.syncVisuals();
    };
    refresh();
    auto has = [&](const std::string& line)
    {
        const auto lines = ui.componentInfo(editor.scene);
        return std::find(lines.begin(), lines.end(), line) != lines.end();
    };
    auto open = [&](int id)
    {
        if (ui.infoComponentId() != -1)
            editor.key(GLFW_KEY_ESCAPE);
        editor.cursor(editor.scene.getCommittedComponentView(id)->getGridPosition());
        editor.mouse(GLFW_MOUSE_BUTTON_RIGHT, GLFW_PRESS);
        editor.mouse(GLFW_MOUSE_BUTTON_RIGHT, GLFW_RELEASE);
        require(
            ui.infoComponentId() == id && editor.input.isIdle(),
            "Right-click did not open component information or started a pan."
        );
    };
    editor.input.setPanOffset({0.1f, 0.05f});
    editor.input.setZoom(1.2f);
    editor.cursor({-6, 4});
    require(ui.infoComponentId() == -1, "Hover opened unwanted component information.");
    const auto revision = editor.scene.getRevision();
    const auto builds = editor.scene.getTopologyBuildCount();
    const auto pan = editor.input.getPanOffset();
    editor.mouse(GLFW_MOUSE_BUTTON_RIGHT, GLFW_PRESS);
    editor.cursorPixels(300, 250);
    editor.mouse(GLFW_MOUSE_BUTTON_RIGHT, GLFW_RELEASE);
    require(
        ui.infoComponentId() == gate && editor.input.getPanOffset() == pan &&
            editor.input.isIdle() && editor.scene.getRevision() == revision &&
            editor.scene.getTopologyBuildCount() == builds,
        "Information click/drag changed camera or committed scene state."
    );
    require(
        has("AND") && has("A: 0 (OFF)") && has("B: 0 (OFF)") && has("Y: 0 (OFF)"),
        "Gate information omitted names or initial indexed states."
    );
    editor.scene.getLogicComponent(gate)->setStateInPin(0, true);
    editor.scene.getLogicComponent(gate)->setStateInPin(1, true);
    refresh();
    require(
        has("A: 1 (ON)") && has("B: 1 (ON)") && has("Y: 1 (ON)"),
        "Open information retained a stale signal snapshot."
    );
    const auto popup = ui.infoBounds(editor.scene);
    editor.cursorPixels(popup.x + 8, popup.y + 12);
    editor.mouse(GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS);
    editor.mouse(GLFW_MOUSE_BUTTON_LEFT, GLFW_RELEASE);
    require(
        ui.infoComponentId() == gate && editor.input.isIdle() &&
            editor.scene.getRevision() == revision,
        "Clicking the popup leaked into component dragging or wiring."
    );
    editor.key(GLFW_KEY_ESCAPE);
    require(
        ui.infoComponentId() == -1 && editor.input.isCanvasFocused(),
        "Escape did not dismiss information and return keyboard focus."
    );

    for (EditorMode mode : {EditorMode::Selection, EditorMode::Interaction})
    {
        editor.input.setMode(mode);
        open(gate);
        editor.cursor({-6, 4});
        const auto start = editor.input.getLastMouse();
        const auto offset = editor.input.getPanOffset();
        editor.mouse(GLFW_MOUSE_BUTTON_MIDDLE, GLFW_PRESS);
        editor.cursorPixels(start.x + 30, start.y + 20);
        editor.mouse(GLFW_MOUSE_BUTTON_MIDDLE, GLFW_RELEASE);
        require(
            ui.infoComponentId() == -1 && editor.input.isIdle() &&
                editor.input.getPanOffset() != offset && editor.scene.getRevision() == revision &&
                editor.scene.getCommittedComponentView(gate)->getGridPosition() ==
                    GridCoords{-6, 4},
            "Middle-drag over a component failed to pan or opened information/edited the scene."
        );
        editor.input.setPanOffset(offset);
    }
    editor.input.setMode(EditorMode::Selection);

    open(latch);
    require(
        has("SR LATCH") && has("S: 0 (OFF)") && has("R: 0 (OFF)") && has("Q: 0 (OFF)") &&
            has("~Q: 1 (ON)"),
        "Latch information confused its independent outputs or labels."
    );
    editor.key(GLFW_KEY_F2);
    open(source);
    require(
        editor.input.getMode() == EditorMode::Interaction && has("Inputs: none") &&
            has("Out: 0 (OFF)"),
        "Information was unavailable in Interaction mode or invented source inputs."
    );
    open(clock);
    editor.key(GLFW_KEY_PERIOD);
    refresh();
    require(has("Out: 1 (ON)"), "Information popup blocked clock controls or failed to update.");

    ComponentDefinition custom;
    custom.identity = {"custom.learning", 1};
    custom.displayName = "Learning gate";
    custom.behavior = OR;
    custom.layout = {
        0.3f,
        0.2f,
        {{"c", "C", PinType::INPUT, 2, {-3, 1}, {}},
         {"a", "A", PinType::INPUT, 0, {-3, 0}, {}},
         {"b", "", PinType::INPUT, 1, {-3, -1}, {}},
         {"out", "Result", PinType::OUTPUT, 0, {3, 0}, {}}}
    };
    custom.presentation.shader = boxShaderResources();
    custom.presentation.bodyLabel = "MY BLOCK";
    const auto result =
        EditorActions(editor.scene)
            .apply(
                {RegisterComponentDefinition{custom}, CreateComponent{custom.identity.id, {0, -12}}}
            );
    require(static_cast<bool>(result), "Cannot create custom information fixture.");
    const int box = result.createdComponentIds.front();
    editor.scene.getLogicComponent(box)->setStateInPin(2, true);
    refresh();
    open(box);
    require(
        has("Learning gate") && has("Label: MY BLOCK") && has("C: 1 (ON)") && has("A: 0 (OFF)") &&
            has("Input 2: 0 (OFF)") && has("Result: 1 (ON)"),
        "Custom information used vector order as pin identity or lost fallback names."
    );
    const auto bounds = ui.infoBounds(editor.scene);
    require(
        bounds.x >= 0 && bounds.y >= 0 && bounds.x + bounds.width <= 800 &&
            bounds.y + bounds.height <= 600,
        "Popup extended past the window edge."
    );
    editor.input.handleFocus(false);
    editor.input.handleFocus(true);
    require(ui.infoComponentId() == -1, "Focus loss left the popup open.");
    open(gate);
    editor.cursorPixels(240, 580);
    editor.mouse(GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS);
    editor.mouse(GLFW_MOUSE_BUTTON_LEFT, GLFW_RELEASE);
    require(
        ui.infoComponentId() == -1 && editor.input.isIdle(),
        "Dismissal click created a canvas gesture."
    );
    const auto beforePan = editor.input.getPanOffset();
    editor.mouse(GLFW_MOUSE_BUTTON_RIGHT, GLFW_PRESS);
    editor.cursorPixels(280, 550);
    editor.mouse(GLFW_MOUSE_BUTTON_RIGHT, GLFW_RELEASE);
    require(
        editor.input.getPanOffset() == beforePan && ui.infoComponentId() == -1,
        "Empty-canvas right-drag still pans."
    );
    editor.mouse(GLFW_MOUSE_BUTTON_MIDDLE, GLFW_PRESS);
    editor.cursorPixels(320, 520);
    editor.mouse(GLFW_MOUSE_BUTTON_MIDDLE, GLFW_RELEASE);
    require(editor.input.getPanOffset() != beforePan, "Empty-canvas middle-drag did not pan.");
    open(gate);
    editor.scene.removeComponent(gate);
    require(ui.componentInfo(editor.scene).empty(), "Deleted component left stale information.");
    editor.cursorPixels(250, 580);
    require(ui.infoComponentId() == -1, "Missing component was not dismissed on input.");

    editor.key(GLFW_KEY_F2);
    editor.cursor({-6, -6});
    editor.mouse(GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS);
    editor.mouse(GLFW_MOUSE_BUTTON_RIGHT, GLFW_PRESS);
    editor.mouse(GLFW_MOUSE_BUTTON_RIGHT, GLFW_RELEASE);
    editor.mouse(GLFW_MOUSE_BUTTON_LEFT, GLFW_RELEASE);
    require(
        ui.infoComponentId() == -1 && editor.input.isIdle(),
        "Right-click inspection replaced active drag cancellation."
    );
    open(latch);
    glfwSetWindowSize(window, 800, 220);
    layout();
    require(ui.infoComponentId() == -1, "Resize retained an obsolete popup layout.");
    editor.input.setPanOffset({0, 0});
    editor.input.setZoom(1);
    open(latch);
    const auto small = ui.infoBounds(editor.scene);
    editor.cursorPixels(small.x + 10, small.y + 50);
    const auto zoom = editor.input.getZoom();
    Input::keyCallback(window, GLFW_KEY_LEFT_CONTROL, 0, GLFW_PRESS, 0);
    Input::scrollCallback(window, 0, -5);
    Input::keyCallback(window, GLFW_KEY_LEFT_CONTROL, 0, GLFW_RELEASE, 0);
    require(
        editor.input.getZoom() == zoom && small.y + small.height <= 220,
        "Information scrolling zoomed the canvas or overflowed a short window."
    );
    editor.key(GLFW_KEY_ESCAPE);
    const int oscillator = editor.scene.addComponent(BuiltinComponentIds::Not, {-10, -12});
    editor.wire({{-9, -12}, {-8, -12}, {-8, -15}, {-12, -15}, {-12, -12}});
    refresh();
    open(oscillator);
    require(
        editor.scene.getLastEvalResult() == SimulationResult::NON_CONVERGENT &&
            has("Y: Unavailable"),
        "Paused simulation information presented a retained output as a valid signal."
    );
    editor.key(GLFW_KEY_ESCAPE);
    editor.input.setUiInputHandler({});
    glfwSetWindowSize(window, 800, 800);
}

void componentNaming(GLFWwindow* window)
{
    glfwSetWindowSize(window, 800, 600);
    Editor editor(window);
    UI ui;
    auto layout = [&]
    {
        ui.layout(
            editor.scene.getComponentCatalog(),
            editor.input.getCameraFrame(window).surface,
            editor.input
        );
    };
    layout();
    editor.input.setUiInputHandler(
        [&](const UiInputEvent& event)
        {
            layout();
            return ui.handleInput(
                event, editor.scene, editor.input, editor.input.getCameraFrame(window)
            );
        }
    );
    const int source = editor.scene.addComponent(BuiltinComponentIds::Input, {-6, 0});
    const int output = editor.scene.addComponent(BuiltinComponentIds::Output, {6, 0});
    editor.wire({{-5, 0}, {5, 0}});
    editor.scene.propagate();
    editor.scene.syncVisuals();
    const auto builds = editor.scene.getTopologyBuildCount();
    auto open = [&](int id)
    {
        editor.cursor(editor.scene.getCommittedComponentView(id)->getGridPosition());
        editor.mouse(GLFW_MOUSE_BUTTON_RIGHT, GLFW_PRESS);
        editor.mouse(GLFW_MOUSE_BUTTON_RIGHT, GLFW_RELEASE);
        require(ui.infoComponentId() == id, "Port information did not open.");
    };
    auto edit = [&]
    {
        const auto bounds = ui.nameBounds(editor.scene);
        editor.cursorPixels(bounds.x + 12, bounds.y + 12);
        editor.mouse(GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS);
        editor.mouse(GLFW_MOUSE_BUTTON_LEFT, GLFW_RELEASE);
    };
    auto type = [&](const std::string& value)
    {
        for (unsigned char c : value)
            Input::charCallback(window, c);
    };
    for (const auto& [id, name] :
         std::vector<std::pair<int, std::string>>{{source, "Data"}, {output, "Sum"}})
    {
        open(id);
        edit();
        require(editor.input.getUiCapture().keyboard, "Name field did not capture typing.");
        const auto count = editor.scene.getComponentCount();
        editor.key(GLFW_KEY_9);
        editor.key(GLFW_KEY_F2);
        type(name + "x");
        editor.key(GLFW_KEY_BACKSPACE);
        require(
            editor.scene.getCommittedComponentView(id)->getBodyLabel().empty() &&
                editor.scene.getComponentCount() == count &&
                editor.input.getMode() == EditorMode::Selection,
            "Draft typing committed early or activated canvas shortcuts."
        );
        editor.key(GLFW_KEY_ENTER);
        require(
            editor.scene.getCommittedComponentView(id)->getBodyLabel() == name &&
                editor.input.getUiCapture() == UiInputCapture{} &&
                editor.scene.getTopologyBuildCount() == builds,
            "Saving a port name changed topology or retained keyboard capture."
        );
        edit();
        type("Discarded");
        editor.key(GLFW_KEY_ESCAPE);
        require(
            ui.infoComponentId() == -1 && !editor.input.getUiCapture().keyboard &&
                editor.scene.getCommittedComponentView(id)->getBodyLabel() == name,
            "Escape saved a draft or retained name editing."
        );
    }
    open(output);
    edit();
    type("Cancelled");
    editor.input.handleFocus(false);
    editor.input.handleFocus(true);
    require(
        ui.infoComponentId() == -1 && !editor.input.getUiCapture().keyboard &&
            editor.scene.getCommittedComponentView(output)->getBodyLabel() == "Sum",
        "Focus loss committed a draft or trapped typing."
    );
    open(source);
    edit();
    type("Outside");
    editor.cursorPixels(790, 590);
    editor.mouse(GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS);
    editor.mouse(GLFW_MOUSE_BUTTON_LEFT, GLFW_RELEASE);
    require(
        ui.infoComponentId() == -1 && !editor.input.getUiCapture().keyboard &&
            editor.scene.getCommittedComponentView(source)->getBodyLabel() == "Data",
        "Outside click committed a name draft."
    );
    open(source);
    edit();
    for (int i = 0; i < 4; ++i)
        editor.key(GLFW_KEY_BACKSPACE);
    type(std::string(40, 'A'));
    Input::charCallback(window, 0xE9);
    editor.key(GLFW_KEY_ENTER);
    require(
        editor.scene.getCommittedComponentView(source)->getBodyLabel() == std::string(32, 'A'),
        "Name field exceeded its printable 32-character limit."
    );
    edit();
    for (int i = 0; i < 32; ++i)
        editor.key(GLFW_KEY_BACKSPACE);
    editor.key(GLFW_KEY_ENTER);
    require(
        editor.scene.getCommittedComponentView(source)->getBodyLabel().empty(),
        "Empty name did not clear the label."
    );
    editor.key(GLFW_KEY_ESCAPE);
    editor.input.setMode(EditorMode::Interaction);
    layout();
    open(output);
    edit();
    require(!editor.input.getUiCapture().keyboard, "Interaction mode allowed port renaming.");
    editor.key(GLFW_KEY_ESCAPE);
    editor.input.setMode(EditorMode::Selection);
    open(output);
    edit();
    const auto deletion = EditorActions(editor.scene).apply({DeleteComponent{output}});
    require(static_cast<bool>(deletion), "Deletion fixture failed.");
    editor.key(GLFW_KEY_ENTER);
    require(
        ui.infoComponentId() == -1 && !editor.input.getUiCapture().keyboard,
        "Deleting the named component retained a stale editor."
    );
    editor.input.setUiInputHandler({});
    glfwSetWindowSize(window, 800, 800);
}
} // namespace

int main(int argc, char** argv)
{
    glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_NULL);
    if (!glfwInit())
    {
        std::cerr << "FAIL: Cannot initialize GLFW null platform." << std::endl;
        return 1;
    }
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWwindow* window = glfwCreateWindow(800, 800, "Input regression tests", nullptr, nullptr);
    if (!window)
    {
        std::cerr << "FAIL: Cannot create headless GLFW window." << std::endl;
        glfwTerminate();
        return 1;
    }

    int result = 0;
    try
    {
        const std::pair<const char*, void (*)(GLFWwindow*)> tests[] = {
            {"drag_cancellation", dragCancellation},
            {"drag_commit", dragCommit},
            {"spawn_placement", spawnPlacement},
            {"wire_segment_deletion", wireSegmentDeletion},
            {"interaction_modes", interactionModes},
            {"mode_cancellation", modeCancellation},
            {"wire_and_pan_gestures", wireAndPanGestures},
            {"ui_input_routing", uiInputRouting},
            {"ui_gesture_cancellation", uiGestureCancellation},
            {"canvas_input_bounds", canvasInputBounds},
            {"canvas_camera_interaction", canvasCameraInteraction},
            {"component_palette", componentPalette},
            {"component_information", componentInformation},
            {"component_naming", componentNaming}
        };
        bool matched = false;
        for (const auto& [name, run] : tests)
        {
            if (argc == 1 || std::string(argv[1]) == name)
            {
                run(window);
                matched = true;
                std::cout << "PASS: " << name << std::endl;
            }
        }
        require(matched, "Unknown test name.");
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL: " << error.what() << std::endl;
        result = 1;
    }
    glfwDestroyWindow(window);
    glfwTerminate();
    return result;
}
