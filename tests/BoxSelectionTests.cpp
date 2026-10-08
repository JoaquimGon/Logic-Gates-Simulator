#include "Components/Definitions/NativeDefinitions.h"
#include "Editor/Actions/EditorActions.h"
#include "Editor/Input.h"
#include "Editor/Scene.h"
#include "Geometry/GridSystem.h"
#include "Persistence/CircuitFile.h"

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

struct Editor
{
    Scene scene;
    Input input;
    GLFWwindow* window;
    int source, first, second, outside;
    WireId internal, extra;

    explicit Editor(GLFWwindow* window) : window(window)
    {
        input.setScene(&scene);
        glfwSetWindowUserPointer(window, &input);
        source = scene.addComponent(BuiltinComponentIds::Input, {-14, 0}, {.inputState = true});
        first = scene.addComponent(BuiltinComponentIds::Not, {-5, 0});
        second = scene.addComponent(BuiltinComponentIds::Not, {5, 0});
        outside = scene.addComponent(BuiltinComponentIds::Clock, {14, 0});
        auto wires = EditorActions(scene).apply(
            {AddWire{{{-13, 0}, {-7, 0}}}, AddWire{{{-4, 0}, {3, 0}}}, AddWire{{{-10, 8}, {10, 8}}}}
        );
        require(static_cast<bool>(wires), "Selection fixture failed.");
        internal = wires.insertedWireIds[1];
        extra = wires.insertedWireIds[2];
        scene.propagate();
    }

    ~Editor() { glfwSetWindowUserPointer(window, nullptr); }

    void cursor(GridCoords grid)
    {
        auto point = input.getCameraFrame(window).worldToWindow(GridSystem::gridToWorld(grid));
        require(point.has_value(), "Test canvas is inactive.");
        glfwSetCursorPos(window, point->x, point->y);
        input.handleCursorPos(window, point->x, point->y);
    }

    void mouse(int action, int mods = 0)
    {
        input.handleMouseButton(window, GLFW_MOUSE_BUTTON_LEFT, action, mods);
    }

    void key(int key)
    {
        input.handleKey(key, GLFW_PRESS);
        input.process(window);
        input.handleKey(key, GLFW_RELEASE);
    }

    void box(GridCoords a = {-9, -4}, GridCoords b = {9, 4})
    {
        cursor(a);
        mouse(GLFW_PRESS, GLFW_MOD_SHIFT);
        cursor(b);
        mouse(GLFW_RELEASE, GLFW_MOD_SHIFT);
    }

    void ctrl(GridCoords point)
    {
        cursor(point);
        mouse(GLFW_PRESS, GLFW_MOD_CONTROL);
        mouse(GLFW_RELEASE, GLFW_MOD_CONTROL);
    }
};

void selection(GLFWwindow* window)
{
    Editor editor(window);
    const auto revision = editor.scene.getRevision();
    editor.cursor({-9, -4});
    editor.mouse(GLFW_PRESS, GLFW_MOD_SHIFT);
    editor.cursor({9, 4});
    require(
        editor.input.getSelectionBox().has_value() && !editor.input.isIdle() &&
            !editor.input.isCurrentlyDrawingWire() && editor.scene.getRevision() == revision,
        "Shift box started a wire or edited the scene."
    );
    editor.mouse(GLFW_RELEASE);
    require(
        editor.input.getSelectedComponents() == std::set<int>{editor.first, editor.second} &&
            editor.input.getSelectedWires() == std::set<WireId>{editor.internal} &&
            !editor.input.getSelectionBox() && editor.scene.getRevision() == revision,
        "Box did not select precisely the fully contained objects."
    );
    editor.box({9, 4}, {-9, -4});
    require(editor.input.getSelectedComponents().size() == 2, "Reverse-direction box failed.");
    editor.ctrl({0, 0});
    require(
        editor.input.getSelectedWires().empty() &&
            editor.input.getSelectedComponents().size() == 2 && editor.input.isIdle(),
        "Ctrl wire toggle cleared gates or started a branch."
    );
    editor.ctrl({0, 0});
    editor.ctrl({0, 8});
    require(
        editor.input.getSelectedWires().size() == 2 && !editor.input.hasSelectedSegment(),
        "Ctrl did not add whole wire sections to the selection."
    );
    editor.box({-6, -1}, {-4, 1});
    require(
        editor.input.getSelectedComponents().empty() && editor.input.getSelectedWires().empty(),
        "Partial component or wire intersection counted as full containment."
    );
    editor.box({-9, -4}, {-2, 4});
    require(
        editor.input.getSelectedComponents() == std::set<int>{editor.first},
        "Single-component box fixture failed."
    );
    editor.cursor({-5, 0});
    editor.mouse(GLFW_PRESS);
    editor.cursor({-5, 6});
    editor.mouse(GLFW_RELEASE);
    require(
        editor.scene.getCommittedComponentView(editor.first)->getGridPosition() ==
            GridCoords{-5, 6},
        "Box-selected single component was deselected instead of dragged."
    );
    editor.input.setMode(EditorMode::Interaction);
    editor.box();
    require(
        editor.input.getSelectedComponents().empty() && editor.input.getSelectedWires().empty() &&
            !editor.input.getSelectionBox(),
        "Interaction mode allowed area selection."
    );
}

void normalizedSelection(GLFWwindow* window);

void movement(GLFWwindow* window)
{
    Editor editor(window);
    editor.box();
    const auto before = circuitToJson(editor.scene, "Group");
    const auto revision = editor.scene.getRevision(), builds = editor.scene.getTopologyBuildCount();
    editor.cursor({-5, 0});
    editor.mouse(GLFW_PRESS);
    editor.cursor({0, 6});
    require(
        editor.scene.getComponentView(editor.first)->getGridPosition() == GridCoords{0, 6} &&
            editor.scene.getComponentView(editor.second)->getGridPosition() == GridCoords{10, 6} &&
            editor.scene.getVisibleWires().at(editor.internal).getPath() ==
                std::vector<GridCoords>{{1, 6}, {8, 6}} &&
            circuitToJson(editor.scene, "Group") == before &&
            editor.scene.getRevision() == revision,
        "Group preview changed committed save data or did not keep relative positions/wires."
    );
    editor.mouse(GLFW_RELEASE);
    editor.scene.propagate();
    require(
        editor.scene.getTopologyBuildCount() == builds + 1 && editor.input.isIdle() &&
            editor.input.getSelectedComponents().size() == 2 &&
            editor.input.getSelectedWires().contains(editor.internal) &&
            editor.scene.getCommittedComponentView(editor.outside)->getGridPosition() ==
                GridCoords{14, 0},
        "Group drop rebuilt repeatedly, moved an unselected item, or lost selection."
    );
    require(
        editor.scene.netOfPin({editor.first, 0}, PinType::INPUT) ==
                editor.scene.netOfPin({editor.source, 0}, PinType::OUTPUT) &&
            editor.scene.netOfPin({editor.second, 0}, PinType::INPUT) ==
                editor.scene.netOfPin({editor.first, 0}, PinType::OUTPUT),
        "Internal wire translated twice or external attachment did not reroute."
    );
    editor.input.cancelCurrentAction();
    editor.ctrl({0, 8});
    editor.cursor({0, 8});
    editor.mouse(GLFW_PRESS);
    editor.cursor({2, 10});
    editor.mouse(GLFW_RELEASE);
    require(
        editor.scene.getWire(editor.extra)->getPath() ==
            std::vector<GridCoords>{{-8, 10}, {12, 10}},
        "Selected wire could not move by itself as a whole section."
    );
    editor.key(GLFW_KEY_DELETE);
    require(
        !editor.scene.getWire(editor.extra) && editor.scene.getComponentCount() == 4,
        "Deleting a Ctrl-selected wire cut a segment or removed a gate."
    );
    normalizedSelection(window);
}

void normalizedSelection(GLFWwindow* window)
{
    Editor editor(window);
    auto branch = EditorActions(editor.scene).apply({AddWire{{{0, 0}, {0, 6}}}});
    require(static_cast<bool>(branch), "Selection seam fixture failed.");
    editor.box();
    require(
        editor.input.getSelectedWires().size() == 2,
        "Branch did not split the selected internal wire."
    );
    editor.cursor({-5, 0});
    editor.mouse(GLFW_PRESS);
    editor.cursor({0, -6});
    editor.mouse(GLFW_RELEASE);
    require(
        editor.input.getSelectedWires().size() == 1 &&
            (editor.scene.getWire(*editor.input.getSelectedWires().begin())->getPath() ==
                 std::vector<GridCoords>{{1, -6}, {8, -6}} ||
             editor.scene.getWire(*editor.input.getSelectedWires().begin())->getPath() ==
                 std::vector<GridCoords>{{8, -6}, {1, -6}}),
        "Healing selected wire sections lost the translated selection."
    );
}

void deletion(GLFWwindow* window)
{
    for (int key : {GLFW_KEY_DELETE, GLFW_KEY_BACKSPACE})
    {
        Editor editor(window);
        editor.box();
        editor.ctrl({0, 8});
        const auto builds = editor.scene.getTopologyBuildCount();
        editor.cursor({0, 12});
        editor.key(key);
        require(
            !editor.scene.getCommittedComponentView(editor.first) &&
                !editor.scene.getCommittedComponentView(editor.second) &&
                editor.scene.getCommittedComponentView(editor.outside) &&
                editor.scene.getCommittedComponentView(editor.source) &&
                !editor.scene.getWire(editor.internal) && !editor.scene.getWire(editor.extra) &&
                editor.scene.getTopologyBuildCount() == builds + 1 &&
                editor.input.getSelectedComponents().empty() &&
                editor.input.getSelectedWires().empty(),
            "Group delete did not delete exactly the selected objects in one rebuild."
        );
    }
}

void cancellation(GLFWwindow* window)
{
    Editor editor(window);
    editor.box();
    const auto before = circuitToJson(editor.scene, "Group");
    const auto revision = editor.scene.getRevision();
    editor.cursor({-5, 0});
    editor.mouse(GLFW_PRESS);
    editor.cursor({4, 0}); // The second selected gate would overlap the unselected clock.
    editor.mouse(GLFW_RELEASE);
    require(
        editor.input.getLastEditError() == EditError::Overlap &&
            circuitToJson(editor.scene, "Group") == before &&
            editor.scene.getRevision() == revision &&
            editor.input.getSelectedComponents().size() == 2,
        "A blocked group move partially committed or lost the selection."
    );
    editor.cursor({-5, 0});
    editor.mouse(GLFW_PRESS);
    editor.cursor({0, 6});
    editor.key(GLFW_KEY_ESCAPE);
    editor.mouse(GLFW_RELEASE);
    require(
        circuitToJson(editor.scene, "Group") == before && editor.input.isIdle() &&
            editor.input.getSelectedComponents().empty(),
        "Escape left a group move or selection behind."
    );
    editor.box();
    editor.cursor({-10, -5});
    editor.mouse(GLFW_PRESS, GLFW_MOD_SHIFT);
    editor.cursor({2, 2});
    editor.input.setUiCapture({true, true});
    editor.input.setUiCapture({});
    editor.input.setCanvasFocused(true);
    editor.mouse(GLFW_RELEASE);
    require(
        !editor.input.getSelectionBox() && editor.input.getSelectedComponents().size() == 2 &&
            editor.scene.getRevision() == revision,
        "UI capture committed a pending box or destroyed the prior selection."
    );
    editor.cursor({-5, 0});
    editor.mouse(GLFW_PRESS);
    editor.cursor({0, 6});
    editor.input.handleFocus(false);
    editor.input.handleFocus(true);
    editor.mouse(GLFW_RELEASE);
    require(
        editor.input.isIdle() && circuitToJson(editor.scene, "Group") == before,
        "Focus loss committed a group preview."
    );
}
} // namespace

int main(int argc, char** argv)
{
    glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_NULL);
    if (!glfwInit())
        return 1;
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    auto* window = glfwCreateWindow(800, 800, "Box selection tests", nullptr, nullptr);
    int status = 0;
    try
    {
        require(window != nullptr, "Cannot create test window.");
        const std::pair<const char*, void (*)(GLFWwindow*)> tests[] = {
            {"box_selection", selection},
            {"group_movement", movement},
            {"group_deletion", deletion},
            {"group_cancellation", cancellation}
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
