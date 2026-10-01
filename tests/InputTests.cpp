#include "Editor/Input.h"
#include "Editor/Scene.h"
#include "Geometry/GridSystem.h"

#include <GLFW/glfw3.h>
#include <cmath>
#include <iostream>
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

    void cursor(GridCoords position)
    {
        int width, height;
        glfwGetWindowSize(window, &width, &height);
        const glm::vec2 world = GridSystem::gridToWorld(position);
        const double x = width * 0.5 + world.x * height * 0.5;
        const double y = height * 0.5 - world.y * height * 0.5;
        glfwSetCursorPos(window, x, y);
        Input::cursorPositionCallback(window, x, y);
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
        const int sink = editor.inverter({outputX + 8, outputY - 1});
        editor.wire({{outputX, outputY}, {outputX + 5, outputY}});
        editor.wire({{outputX + 1, outputY - 1}, {outputX + 6, outputY - 1}});

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
            editor.scene.getComponentView(spawned)->getGridPosition() == GridCoords{1, -1},
            "Spawn did not resolve the occupied position."
        );
        require(!editor.scene.checkOverlap(spawned), "Spawn left an overlapping component.");
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
    const int clock = editor.scene.addClock({20, 0}, {0.15f, 0.15f}, "clock");
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
    for (GridCoords body : {GridCoords{12, 0}, GridCoords{20, 0}})
    {
        editor.cursor(body);
        editor.mouse(GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS);
        editor.cursor({body.x, body.y + 4});
        editor.mouse(GLFW_MOUSE_BUTTON_LEFT, GLFW_RELEASE);
    }
    require(
        editor.scene.getCommittedComponentView(gate)->getGridPosition() == GridCoords{12, 0} &&
            editor.scene.getCommittedComponentView(clock)->getGridPosition() == GridCoords{20, 0},
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
        editor.mouse(GLFW_MOUSE_BUTTON_RIGHT, GLFW_PRESS);
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
        editor.mouse(GLFW_MOUSE_BUTTON_RIGHT, GLFW_RELEASE);
        editor.mouse(GLFW_MOUSE_BUTTON_LEFT, GLFW_RELEASE);
        require(
            editor.input.isIdle() && editor.input.getPanOffset() == finishedOffset &&
                editor.scene.getWireIds() == wireIds,
            "Mode change left panning or editing active."
        );
    }
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
            {"wire_and_pan_gestures", wireAndPanGestures}
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