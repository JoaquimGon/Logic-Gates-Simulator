#include "Core/Input.h"

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
    for (int cancelMethod = 0; cancelMethod < 3; ++cancelMethod)
    {
        Editor editor(window);
        editor.scene.addInputPin({-8, 0}, {0.15f, 0.15f}, "inputPin", true);
        const int sink = editor.inverter({0, 0});
        editor.wire({{-7, 0}, {-2, 0}});
        editor.scene.propagate();
        const auto wireIds = editor.scene.getWireIds();

        editor.cursor({0, 0});
        editor.mouse(GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS);
        require(!editor.input.isIdle(), "Component body did not start dragging.");
        editor.cursor({6, 4});
        require(
            editor.scene.getComponentView(sink)->getGridPosition() == GridCoords{6, 4},
            "Drag did not preview the new position."
        );

        if (cancelMethod == 0)
            editor.key(GLFW_KEY_ESCAPE);
        else if (cancelMethod == 1)
            editor.mouse(GLFW_MOUSE_BUTTON_RIGHT, GLFW_PRESS);
        else
            editor.input.cancelCurrentAction();

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
        editor.cursor(target);
        editor.mouse(GLFW_MOUSE_BUTTON_LEFT, GLFW_RELEASE);

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
        editor.key(key);
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
            {"spawn_placement", spawnPlacement}
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