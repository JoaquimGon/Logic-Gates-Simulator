#include "Components/Definitions/NativeDefinitions.h"
#include "Editor/Actions/EditHistory.h"
#include "Editor/Actions/EditorActions.h"
#include "Editor/CircuitViews.h"
#include "Editor/Input.h"
#include "Editor/Subcircuits.h"
#include "Geometry/GridSystem.h"
#include "Persistence/CircuitFile.h"
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

EditResult edit(Scene& scene, EditHistory& history, EditBatch batch)
{
    auto result = EditorActions(scene).apply(batch);
    require(static_cast<bool>(result), "History fixture edit failed.");
    history.record(result);
    return result;
}

void limits(GLFWwindow*)
{
    Scene scene;
    EditHistory history;
    const int gate = scene.addComponent(BuiltinComponentIds::And, {0, 0});
    std::weak_ptr<const EditRecord> oldest;
    for (int i = 1; i <= 105; ++i)
    {
        auto result = edit(
            scene,
            history,
            {ConfigureComponentProperties{.componentId = gate, .label = std::to_string(i)}}
        );
        if (i == 1)
            oldest = result.change;
    }
    require(
        oldest.expired() && history.undoCount() == 100, "History failed to evict its oldest record."
    );
    for (int i = 0; i < 100; ++i)
        require(static_cast<bool>(history.undo(scene)), "Undo failed.");
    require(
        scene.getCommittedComponentView(gate)->getBodyLabel() == "5" && history.undoCount() == 0 &&
            history.redoCount() == 100,
        "Undo exceeded its 100-step boundary or restored the wrong step."
    );
    const auto revision = scene.getRevision();
    require(
        static_cast<bool>(history.undo(scene)) && scene.getRevision() == revision,
        "Empty undo changed the scene."
    );
    for (int i = 0; i < 100; ++i)
        require(static_cast<bool>(history.redo(scene)), "Redo failed.");
    require(
        scene.getCommittedComponentView(gate)->getBodyLabel() == "105" &&
            history.undoCount() == 100 && history.redoCount() == 0,
        "Redo lost bounded records or their ordering."
    );
    history.undo(scene);
    history.record(EditorActions(scene).apply({MoveComponent{gate, {0, 0}}}));
    history.record(EditorActions(scene).apply({DeleteComponent{999}}));
    require(history.redoCount() == 1, "A no-op or rejected edit discarded redo.");
    edit(scene, history, {ConfigureComponentProperties{.componentId = gate, .label = "branch"}});
    require(history.redoCount() == 0, "Editing after undo retained an invalid redo branch.");
}

void groups(GLFWwindow*)
{
    Scene scene;
    EditHistory history;
    const int first = scene.addComponent(BuiltinComponentIds::Input, {-8, 0}, {.inputState = true});
    const int second = scene.addComponent(BuiltinComponentIds::Not, {0, 0});
    const auto wire = scene.commitWire(
        []
        {
            Wire w;
            w.setPath({{-7, 0}, {-2, 0}});
            return w;
        }()
    );
    scene.propagate();
    const auto before = circuitToJson(scene, "History");
    edit(
        scene,
        history,
        {MoveComponent{first, {-8, 6}}, MoveComponent{second, {0, 6}}, MoveWire{*wire, {0, 6}}}
    );
    const auto after = circuitToJson(scene, "History");
    require(history.undoCount() == 1, "Group movement created multiple history entries.");
    const auto builds = scene.getTopologyBuildCount();
    require(
        static_cast<bool>(history.undo(scene)) && scene.getTopologyBuildCount() == builds + 1 &&
            circuitToJson(scene, "History") == before,
        "Undo did not restore group/wire geometry once."
    );
    require(
        static_cast<bool>(history.redo(scene)) && circuitToJson(scene, "History") == after &&
            scene.getLogicComponent(second)->getStateInPin(0),
        "Redo did not restore shape or signal connectivity."
    );
    edit(scene, history, {DeleteComponent{first}, DeleteComponent{second}, DeleteWire{*wire}});
    require(
        static_cast<bool>(history.undo(scene)) && scene.getComponentCount() == 2 &&
            scene.wireCount() == 1,
        "Undo of group deletion did not restore all objects."
    );
    require(
        static_cast<bool>(history.redo(scene)) && scene.getComponentCount() == 0 &&
            scene.wireCount() == 0,
        "Redo of group deletion left partial objects."
    );
    const auto previewGate = scene.addComponent(BuiltinComponentIds::And, {10, 0});
    auto preview = EditorActions(scene).beginMove(previewGate);
    require(
        history.undo(scene).error == EditError::PreviewActive && history.undoCount() == 2,
        "Active preview mutated the history stacks."
    );
    EditorActions(scene).cancelMove(*preview);
}

void shortcuts(GLFWwindow* window)
{
    Scene scene;
    Input input;
    input.setScene(&scene);
    auto key = [&](int key, int mods = 0)
    {
        input.handleKey(key, GLFW_PRESS, mods);
        input.process(window);
        input.handleKey(key, GLFW_RELEASE, mods);
    };
    auto cursor = [&](GridCoords grid)
    {
        const auto point =
            input.getCameraFrame(window).worldToWindow(GridSystem::gridToWorld(grid));
        glfwSetCursorPos(window, point->x, point->y);
        input.handleCursorPos(window, point->x, point->y);
    };
    cursor({0, 0});
    key(GLFW_KEY_3);
    require(
        scene.getComponentCount() == 1 && input.getUndoCount() == 1,
        "Keyboard creation was not recorded."
    );
    key(GLFW_KEY_Z, GLFW_MOD_CONTROL);
    require(scene.getComponentCount() == 0 && input.getRedoCount() == 1, "Ctrl+Z did not undo.");
    key(GLFW_KEY_Z, GLFW_MOD_CONTROL | GLFW_MOD_SHIFT);
    require(
        scene.getComponentCount() == 1 && input.getUndoCount() == 1, "Ctrl+Shift+Z did not redo."
    );
    key(GLFW_KEY_Z, GLFW_MOD_CONTROL);
    key(GLFW_KEY_Y, GLFW_MOD_CONTROL);
    require(scene.getComponentCount() == 1, "Ctrl+Y did not redo.");
    input.setMode(EditorMode::Interaction);
    key(GLFW_KEY_Z, GLFW_MOD_CONTROL);
    require(scene.getComponentCount() == 1, "Interaction mode undid structural edits.");
    input.setMode(EditorMode::Selection);
    input.setUiInputHandler([](const UiInputEvent& event)
                            { return event.kind == UiInputKind::Key; });
    key(GLFW_KEY_Z, GLFW_MOD_CONTROL);
    require(scene.getComponentCount() == 1, "Captured text input triggered circuit undo.");
    input.setUiInputHandler({});
    input.setCanvasFocused(true);
    cursor({0, 0});
    input.handleMouseButton(window, GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS, 0);
    cursor({0, 6});
    key(GLFW_KEY_Z, GLFW_MOD_CONTROL);
    input.handleMouseButton(window, GLFW_MOUSE_BUTTON_LEFT, GLFW_RELEASE, 0);
    require(
        input.isIdle() && scene.getComponentCount() == 1 && input.getUndoCount() == 1 &&
            scene.getCommittedComponentView(0)->getGridPosition() == GridCoords{0, 0},
        "Undo consumed history instead of cancelling the pending move."
    );
    key(GLFW_KEY_Z, GLFW_MOD_CONTROL);
    require(scene.getComponentCount() == 0, "Second undo did not undo the committed edit.");
}

void views(GLFWwindow* window)
{
    CircuitViews views;
    Input input;
    views.select(0, input);
    auto record = [&](EditBatch batch)
    {
        auto result = EditorActions(views.activeScene()).apply(batch);
        require(static_cast<bool>(result), "View edit failed.");
        input.recordEdit(result);
    };
    auto undo = [&]
    {
        input.handleKey(GLFW_KEY_Z, GLFW_PRESS, GLFW_MOD_CONTROL);
        input.handleKey(GLFW_KEY_Z, GLFW_RELEASE, GLFW_MOD_CONTROL);
    };
    record({CreateComponent{BuiltinComponentIds::Input, {0, 0}}});
    views.markSaved(0);
    record({ConfigureComponentProperties{.componentId = 0, .label = "Main signal"}});
    require(views.hasUnsavedChanges(0), "View did not mark an edit unsaved.");
    undo();
    require(!views.hasUnsavedChanges(0), "Undo to the saved design did not clear unsaved status.");
    views.create(input, CircuitViews::Role::Subcircuit);
    record(
        {CreateComponent{BuiltinComponentIds::Input, {-10, 0}},
         CreateComponent{BuiltinComponentIds::Output, {10, 0}}}
    );
    require(input.getUndoCount() == 1, "A new editor inherited Main's history.");
    views.activeDefinition() = {"subcircuit.author-history", 1};
    const auto connected = EditorActions(views.activeScene()).apply({AddWire{{{-9, 0}, {9, 0}}}});
    input.recordEdit(connected);
    views.publishSubcircuits(
        {makeSubcircuit(views.activeScene(), views.activeDefinition(), "Author")}
    );
    views.markSaved(1);
    require(
        input.getUndoCount() == 2, "Publishing/saving the authored design cleared its history."
    );
    views.select(0, input);
    require(
        input.getUndoCount() == 1 && input.getRedoCount() == 1,
        "Switching tabs lost Main's history."
    );
    // Add/remove tabs after binding: the history address must remain valid.
    for (int i = 0; i < 8; ++i)
        views.create(input);
    views.select(1, input);
    require(input.getUndoCount() == 2, "Growing the view vector invalidated history ownership.");
    views.remove(2, input);
    undo();
    undo();
    require(
        views.activeScene().getComponentCount() == 0 && views.mainScene().getComponentCount() == 1,
        "Subcircuit undo changed another editor."
    );
    views.select(0, input);
    const Scene saved(views.mainScene());
    views.replaceWorkspace(saved, "Opened", input);
    require(
        input.getUndoCount() == 0 && input.getRedoCount() == 0,
        "Open retained history for the replaced file."
    );
    input.handleKey(GLFW_KEY_Z, GLFW_PRESS, GLFW_MOD_CONTROL);
    input.handleKey(GLFW_KEY_Z, GLFW_REPEAT, GLFW_MOD_CONTROL);
    input.handleKey(GLFW_KEY_Z, GLFW_RELEASE, GLFW_MOD_CONTROL);
    require(
        views.mainScene().getComponentCount() == 1,
        "Empty/repeated undo changed a freshly opened file."
    );
}

void library(GLFWwindow*)
{
    CircuitViews views;
    Input input;
    views.select(0, input);
    auto creation =
        EditorActions(views.mainScene()).apply({CreateComponent{BuiltinComponentIds::And, {0, 0}}});
    input.recordEdit(creation);
    Scene authored;
    authored.addComponent(BuiltinComponentIds::Input, {-10, 0});
    authored.addComponent(BuiltinComponentIds::Output, {10, 0});
    authored.setInterfaceNamingRequired(true);
    const auto connected = EditorActions(authored).apply({AddWire{{{-9, 0}, {9, 0}}}});
    require(static_cast<bool>(connected), "Library history fixture failed.");
    auto definition = makeSubcircuit(authored, {"subcircuit.history", 1}, "Reusable");
    views.publishSubcircuits({definition});
    require(input.getUndoCount() == 1, "Unused library import cleared unrelated editing history.");
    input.handleKey(GLFW_KEY_Z, GLFW_PRESS, GLFW_MOD_CONTROL);
    input.handleKey(GLFW_KEY_Z, GLFW_RELEASE, GLFW_MOD_CONTROL);
    require(
        views.mainScene().getComponentCount() == 0 &&
            views.mainScene().getComponentCatalog().find(definition.identity.id),
        "Undoing an edit removed a subsequently imported library entry."
    );
    auto placed =
        EditorActions(views.mainScene()).apply({CreateComponent{definition.identity.id, {0, 0}}});
    input.recordEdit(placed);
    definition = makeSubcircuit(authored, {"subcircuit.history", 2}, "Reusable");
    views.publishSubcircuits({definition});
    require(
        input.getUndoCount() == 0 && input.getRedoCount() == 0,
        "External used-definition update retained incompatible history."
    );
}
} // namespace

int main(int argc, char** argv)
{
    glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_NULL);
    if (!glfwInit())
        return 1;
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    auto* window = glfwCreateWindow(800, 800, "Edit history tests", nullptr, nullptr);
    int status = 0;
    try
    {
        require(window != nullptr, "Cannot create headless test window.");
        const std::pair<const char*, void (*)(GLFWwindow*)> tests[] = {
            {"history_limits", limits},
            {"history_groups", groups},
            {"history_shortcuts", shortcuts},
            {"history_views", views},
            {"history_library", library}
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
