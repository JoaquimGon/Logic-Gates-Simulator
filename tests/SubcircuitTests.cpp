#include "App/CircuitFiles.h"
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
#include <chrono>
#include <filesystem>
#include <iostream>
#include <nlohmann/json.hpp>
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

template <class F>
void rejects(F action, const char* message)
{
    try
    {
        action();
    }
    catch (const std::invalid_argument&)
    {
        return;
    }
    throw std::runtime_error(message);
}

Scene wireDesign(bool inverted = false)
{
    Scene scene;
    // Leave an ID gap to check persistence's explicit component-order port mapping.
    const int removed = scene.addComponent(BuiltinComponentIds::And, {60, 0});
    scene.removeComponent(removed);
    const int input = scene.addComponent(BuiltinComponentIds::Input, {-20, 0});
    const int output = scene.addComponent(BuiltinComponentIds::Output, {20, 0});
    edit(
        scene,
        {ConfigureComponentProperties{.componentId = input, .label = "Data"},
         ConfigureComponentProperties{.componentId = output, .label = "Result"}}
    );
    if (inverted)
    {
        scene.addComponent(BuiltinComponentIds::Not, {0, 0});
        edit(scene, {AddWire{{{-19, 0}, {-2, 0}}}, AddWire{{{1, 0}, {19, 0}}}});
    }
    else
        edit(scene, {AddWire{{{-19, 0}, {-19, 6}, {19, 6}, {19, 0}}}});
    scene.setInterfaceNamingRequired(true);
    scene.propagate();
    return scene;
}

ComponentDefinition definition(bool inverted = false, unsigned version = 1)
{
    auto scene = wireDesign(inverted);
    return makeSubcircuit(scene, {"subcircuit.test", version}, "Example");
}

void runtime()
{
    auto design = definition();
    Circuit circuit;
    const auto& behavior = std::get<SubcircuitBehavior>(design.behavior);
    const int a = circuit.addComponent(std::make_unique<Subcircuit>(behavior));
    const int b = circuit.addComponent(std::make_unique<Subcircuit>(behavior));
    const int source = circuit.addInputPin(true);
    require(circuit.connectComponents(source, 0, a, 0), "Composite input rejected.");
    require(
        circuit.propagate() == SimulationResult::OK && circuit.getComponent(a)->getStateOutPin(0) &&
            !circuit.getComponent(b)->getStateOutPin(0),
        "Composite input states are shared or misrouted."
    );
    Circuit copy = circuit;
    static_cast<InputPin*>(copy.getComponent(source))->setState(false);
    copy.propagate();
    require(
        circuit.getComponent(a)->getStateOutPin(0) && !copy.getComponent(a)->getStateOutPin(0),
        "Snapshot copied subcircuit runtime by reference."
    );

    copy.replaceComponent(a, std::make_unique<Subcircuit>(behavior));
    require(
        copy.getComponent(source)->getOutConnections().empty() &&
            copy.getComponent(a)->getInConnections().empty(),
        "Replacement retained asymmetric old edges."
    );
    Scene memory;
    const int s = memory.addComponent(BuiltinComponentIds::Input, {-20, 5});
    const int r = memory.addComponent(BuiltinComponentIds::Input, {-20, -5});
    memory.addComponent(BuiltinComponentIds::SrLatch, {0, 0});
    const int q = memory.addComponent(BuiltinComponentIds::Output, {20, 5});
    const int nq = memory.addComponent(BuiltinComponentIds::Output, {20, -5});
    edit(
        memory,
        {ConfigureComponentProperties{.componentId = s, .label = "Set"},
         ConfigureComponentProperties{.componentId = r, .label = "Reset"},
         ConfigureComponentProperties{.componentId = q, .label = "Q"},
         ConfigureComponentProperties{.componentId = nq, .label = "~Q"},
         AddWire{{{-19, 5}, {-3, 5}, {-3, 1}}},
         AddWire{{{-19, -5}, {-3, -5}, {-3, -1}}},
         AddWire{{{3, 1}, {19, 1}, {19, 5}}},
         AddWire{{{3, -1}, {19, -1}, {19, -5}}}}
    );
    memory.handleClick(s);
    memory.propagate();
    memory.handleClick(s);
    memory.propagate();
    require(
        memory.getLogicComponent(2)->getStateOutPin(0), "Authored retained-state fixture failed."
    );
    const auto latch = makeSubcircuit(memory, {"subcircuit.memory", 1}, "Memory");
    Circuit memories;
    const int first = memories.addComponent(
        std::make_unique<Subcircuit>(std::get<SubcircuitBehavior>(latch.behavior))
    );
    const int second = memories.addComponent(
        std::make_unique<Subcircuit>(std::get<SubcircuitBehavior>(latch.behavior))
    );
    const int set = memories.addInputPin(true);
    memories.connectComponents(set, 0, first, 0);
    memories.propagate();
    static_cast<InputPin*>(memories.getComponent(set))->setState(false);
    memories.propagate();
    require(
        memories.getComponent(first)->getStateOutPin(0) &&
            !memories.getComponent(first)->getStateOutPin(1) &&
            !memories.getComponent(second)->getStateOutPin(0) &&
            memories.getComponent(second)->getStateOutPin(1),
        "Latch memory or indexed outputs are shared across instances."
    );

    Scene wide;
    for (int i = 0; i < 4; ++i)
        wide.addComponent(BuiltinComponentIds::Input, {-20, i * 10});
    for (int i = 0; i < 3; ++i)
    {
        wide.addComponent(BuiltinComponentIds::Output, {20, i * 10});
        edit(wide, {AddWire{{{-19, i * 10}, {19, i * 10}}}});
    }
    wide.setInterfaceNamingRequired(true);
    const auto multiple =
        makeSubcircuit(wide, {"subcircuit.multiple", 1}, "Four inputs, three outputs");
    Subcircuit many(std::get<SubcircuitBehavior>(multiple.behavior));
    many.setStateInPin(0, true);
    many.setStateInPin(2, true);
    many.evaluate();
    require(
        many.getInputPinCount() == 4 && many.getOutputPinCount() == 3 && many.getStateOutPin(0) &&
            !many.getStateOutPin(1) && many.getStateOutPin(2),
        "Arbitrary subcircuit input/output indices were truncated."
    );

    const auto inverter = definition(true);
    Circuit feedback;
    const int loop = feedback.addComponent(
        std::make_unique<Subcircuit>(std::get<SubcircuitBehavior>(inverter.behavior))
    );
    feedback.connectComponents(loop, 0, loop, 0);
    require(
        feedback.propagate() == SimulationResult::NON_CONVERGENT,
        "Feedback bypassed convergence limit."
    );
    std::size_t budget = 1;
    require(
        circuit.propagate(budget) == SimulationResult::NON_CONVERGENT && budget == 0,
        "Child circuit bypassed its parent's evaluation budget."
    );
}

void clocks()
{
    Scene scene;
    scene.addComponent(BuiltinComponentIds::Input, {-20, 10});
    scene.addComponent(BuiltinComponentIds::Clock, {0, 0});
    scene.addComponent(BuiltinComponentIds::Output, {20, 0});
    edit(scene, {AddWire{{{1, 0}, {19, 0}}}});
    scene.setInterfaceNamingRequired(true);
    static_cast<Clock*>(scene.getLogicComponent(1))->advanceSlice(.25);
    auto clocked = makeSubcircuit(scene, {"subcircuit.clocked", 1}, "Clocked");
    Scene nested;
    edit(nested, {RegisterComponentDefinition{clocked}});
    nested.addComponent(BuiltinComponentIds::Input, {-20, 10});
    nested.addComponent(clocked.identity.id, {0, 0});
    nested.addComponent(BuiltinComponentIds::Output, {20, 0});
    edit(nested, {AddWire{{{4, 0}, {19, 0}}}});
    nested.setInterfaceNamingRequired(true);
    auto wrapper = makeSubcircuit(nested, {"subcircuit.nested", 1}, "Nested clock");
    const auto names = subcircuitClockNames(nested);
    require(
        names.size() == 1 && names[0].find("clock 1") != std::string::npos,
        "Subcircuit overview omitted its nested clocks."
    );
    Circuit circuit;
    const int first = circuit.addComponent(
        std::make_unique<Subcircuit>(std::get<SubcircuitBehavior>(wrapper.behavior))
    );
    circuit.propagate();
    require(!circuit.getComponent(first)->getStateOutPin(0), "Clock started high.");
    require(
        circuit.updateClocks(.5f) && circuit.getComponent(first)->getStateOutPin(0),
        "Nested rising clock edge lost."
    );
    const int second = circuit.addComponent(
        std::make_unique<Subcircuit>(std::get<SubcircuitBehavior>(wrapper.behavior))
    );
    circuit.propagate();
    require(
        circuit.getComponent(first)->getStateOutPin(0) &&
            !circuit.getComponent(second)->getStateOutPin(0),
        "New placement reused another instance's clock phase."
    );
    circuit.updateClocks(.5f);
    require(
        !circuit.getComponent(first)->getStateOutPin(0) &&
            circuit.getComponent(second)->getStateOutPin(0),
        "Nested clocks did not advance chronologically with independent phases."
    );
    std::vector<Clock*> collected;
    circuit.collectClocks(collected);
    require(collected.size() == 2, "Nested clock enumeration failed.");
}

void files()
{
    const auto scene = wireDesign();
    const auto text = subcircuitToJson(scene, {"subcircuit.test", 7}, "Example");
    auto saved = subcircuitFromJson(text);
    require(
        saved.identity.version == 7 &&
            subcircuitToJson(saved.design.scene, saved.identity, saved.design.name) == text,
        "Authored layout, bends, labels or port order changed on round trip."
    );
    auto document = nlohmann::json::parse(text);
    document["ports"][0]["component"] = 500;
    rejects(
        [&] { subcircuitFromJson(document.dump()); }, "Invalid external port mapping accepted."
    );
    Scene draft;
    auto loadedDraft =
        subcircuitFromJson(subcircuitToJson(draft, {"subcircuit.draft", 1}, "Draft"));
    require(
        loadedDraft.design.scene.requiresInterfaceNames() &&
            loadedDraft.design.scene.getComponentCount() == 0,
        "Invalid drafts cannot be saved or edited."
    );
    rejects(
        [&] { makeSubcircuit(loadedDraft.design.scene, loadedDraft.identity, "Draft"); },
        "Invalid draft was publishable."
    );
    Scene duplicate = wireDesign();
    const int extra = duplicate.addComponent(BuiltinComponentIds::Input, {-40, 10});
    edit(duplicate, {ConfigureComponentProperties{.componentId = extra, .label = "Data"}});
    require(!subcircuitProblem(duplicate).empty(), "Duplicate input labels were publishable.");

    Scene main;
    const auto custom = makeSubcircuit(saved.design.scene, saved.identity, saved.design.name);
    edit(main, {RegisterComponentDefinition{custom}, CreateComponent{custom.identity.id, {0, 0}}});
    main.addComponent(BuiltinComponentIds::Input, {-20, 0}, {.inputState = true});
    main.addComponent(BuiltinComponentIds::Output, {20, 0});
    edit(main, {AddWire{{{-19, 0}, {-4, 0}}}, AddWire{{{4, 0}, {19, 0}}}});
    const auto mainJson = circuitToJson(main, "Main");
    const auto path =
        std::filesystem::temp_directory_path() /
        ("logic-subcircuit-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".json");
    saveSubcircuit(path, scene, {"subcircuit.test", 7}, "Example");
    require(
        loadSubcircuit(path).identity.id == "subcircuit.test", "Standalone subcircuit I/O failed."
    );
    std::filesystem::remove(path);
    auto restored = circuitFromJson(mainJson);
    require(
        restored.scene.getLogicComponent(0)->getStateOutPin(0) &&
            circuitToJson(restored.scene, "Main") == mainJson,
        "Main did not embed a complete runnable definition after source file removal."
    );
    const auto& behavior = std::get<SubcircuitBehavior>(
        restored.scene.getComponentCatalog().find(custom.identity.id)->behavior
    );
    require(
        circuitToJson(*behavior.authored, "Example") ==
            circuitToJson(saved.design.scene, "Example"),
        "Embedded authored scene cannot be reopened for editing."
    );
    auto self = restored.scene;
    self.setInterfaceNamingRequired(true);
    rejects(
        [&] { makeSubcircuit(self, custom.identity, "Recursive"); },
        "Definition could contain itself."
    );
}

void editing()
{
    Input input;
    CircuitViews views;
    views.select(0, input);
    auto original = definition();
    views.publishSubcircuits({original});
    const int id = views.mainScene().addComponent(original.identity.id, {0, 0});
    views.editSubcircuit(original, input);
    require(
        views.size() == 2 && views.role(1) == CircuitViews::Role::Subcircuit &&
            circuitToJson(views.activeScene(), "Example") ==
                circuitToJson(*std::get<SubcircuitBehavior>(original.behavior).authored, "Example"),
        "Edit did not reopen the authored scene."
    );
    views.rename(1, "Unsaved editor name");
    views.editSubcircuit(original, input);
    require(
        views.size() == 2 && views.name(1) == "Unsaved editor name",
        "Edit discarded an existing unsaved editor."
    );
    const auto replacement = definition(true, 2);
    const auto before = circuitToJson(views.mainScene(), "Main");
    views.publishSubcircuits({replacement});
    require(
        views.mainScene().getCommittedComponentView(id)->getDefinitionIdentity().version == 2,
        "Explicit publish did not update placed instances."
    );
    views.mainScene().propagate();
    require(
        views.mainScene().getLogicComponent(id)->getStateOutPin(0),
        "Published behavior was not installed."
    );
    const auto stable = circuitToJson(views.mainScene(), "Main");
    auto incompatible = wireDesign(true);
    const int extra = incompatible.addComponent(BuiltinComponentIds::Input, {-40, 10});
    edit(incompatible, {ConfigureComponentProperties{.componentId = extra, .label = "Extra"}});
    auto changed = makeSubcircuit(incompatible, {"subcircuit.test", 3}, "Changed");
    rejects(
        [&] { views.publishSubcircuits({changed}); }, "Changed used interface was silently applied."
    );
    require(
        circuitToJson(views.mainScene(), "Main") == stable && views.activeScene()
                                                                      .getComponentCatalog()
                                                                      .find(original.identity.id)
                                                                      ->identity.version == 2,
        "Rejected publish partially changed the session."
    );
    views.setActiveFile("example.json");
    views.create(input);
    require(
        views.activeFile().empty() &&
            views.activeScene().getComponentCatalog().find(original.identity.id),
        "New workspace inherited an editor path or lost the Custom library."
    );
    views.select(1, input);
    require(
        views.activeFile() == "example.json", "Subcircuit file association was lost on tab switch."
    );
    views.replaceMain(circuitFromJson(before).scene, "Older Main", input);
    require(
        views.activeIndex() == 0 &&
            views.mainScene().getComponentCatalog().find(original.identity.id)->identity.version ==
                1,
        "Main open did not adopt embedded definitions."
    );
    input.setScene(nullptr);
}

void fileCommands()
{
    const auto directory =
        std::filesystem::temp_directory_path() /
        ("logic-subcircuit-commands-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directory(directory);
    const auto first = directory / "first.json", second = directory / "second.json",
               mainPath = directory / "main.json";
    Input input;
    CircuitViews views;
    views.select(0, input);
    UI ui;
    views.create(input, CircuitViews::Role::Subcircuit);
    edit(
        views.activeScene(),
        {CreateComponent{BuiltinComponentIds::Input, {-20, 0}},
         CreateComponent{BuiltinComponentIds::Output, {20, 0}},
         AddWire{{{-19, 0}, {19, 0}}}}
    );
    views.rename(1, "Reusable");
    require(views.hasUnsavedChanges(1), "New subcircuit was shown as saved.");
    applyFileCommand(UI::FileCommand::SaveAs, std::nullopt, ui, views, input);
    require(
        views.activeDefinition().id.empty() && views.activeFile().empty() &&
            views.hasUnsavedChanges(1),
        "Cancelled first save assigned an identity/path."
    );
    applyFileCommand(UI::FileCommand::Save, first, ui, views, input);
    const auto identity = views.activeDefinition();
    require(
        views.activeFile() == first && !views.hasUnsavedChanges(1) && views.hasUnsavedChanges(0) &&
            views.mainScene().getComponentCatalog().find(identity.id),
        "Successful subcircuit save lost its path or did not publish."
    );
    views.rename(1, "Renamed");
    require(views.hasUnsavedChanges(1), "Rename did not mark the subcircuit unsaved.");
    performFileCommand(UI::FileCommand::Save, nullptr, ui, views, input);
    require(loadSubcircuit(first).design.name == "Renamed", "Save did not reuse the current file.");
    applyFileCommand(UI::FileCommand::SaveAs, second, ui, views, input);
    require(
        views.activeFile() == second && std::filesystem::exists(first),
        "Save As did not preserve the previous file/change association."
    );
    const auto goodIdentity = views.activeDefinition();
    views.rename(1, "Edited before failure");
    bool failed = false;
    try
    {
        applyFileCommand(
            UI::FileCommand::SaveAs, directory / "missing" / "failure.json", ui, views, input
        );
    }
    catch (const std::exception&)
    {
        failed = true;
    }
    require(
        failed && views.activeFile() == second && views.hasUnsavedChanges(1) &&
            views.activeDefinition().version == goodIdentity.version,
        "Failed Save As changed path or definition identity."
    );
    applyFileCommand(UI::FileCommand::SaveAs, std::nullopt, ui, views, input);
    require(
        views.activeFile() == second && views.hasUnsavedChanges(1),
        "Cancelled Save As changed association or saved status."
    );
    const int custom = views.mainScene().addComponent(identity.id, {0, 0});
    // An incompatible draft must be saved, while all placed wiring/interfaces stay untouched.
    views.activeScene().addComponent(BuiltinComponentIds::Input, {-40, 10});
    const auto publishedVersion =
        views.mainScene().getComponentCatalog().find(identity.id)->identity.version;
    performFileCommand(UI::FileCommand::Save, nullptr, ui, views, input);
    require(
        !views.hasUnsavedChanges(1) &&
            loadSubcircuit(second).design.scene.getComponentCount() == 3 &&
            views.mainScene().getComponentCatalog().find(identity.id)->identity.version ==
                publishedVersion &&
            views.mainScene().getLogicComponent(custom)->getInputPinCount() == 1,
        "Rejected publication lost its saved draft or changed placed interfaces."
    );
    views.select(0, input);
    applyFileCommand(UI::FileCommand::SaveAs, mainPath, ui, views, input);
    require(
        views.activeFile() == mainPath && !views.hasUnsavedChanges(0),
        "Main lost its independent save association."
    );
    const auto workspacePath = directory / "workspace.json";
    views.create(input);
    require(
        views.hasUnsavedChanges(2) && views.activeFile().empty(),
        "New workspace reused another tab's file/status."
    );
    views.activeScene().addComponent(BuiltinComponentIds::Not, {0, 0});
    applyFileCommand(UI::FileCommand::SaveAs, workspacePath, ui, views, input);
    require(
        !views.hasUnsavedChanges(2) && loadCircuit(workspacePath).scene.getComponentCount() == 1 &&
            loadCircuit(mainPath).scene.getLogicComponent(custom),
        "Saving an active workspace overwrote Main or saved the wrong scene."
    );
    views.rename(2, "Another workspace");
    performFileCommand(UI::FileCommand::Save, nullptr, ui, views, input);
    require(
        loadCircuit(workspacePath).name == "Another workspace",
        "Workspace Save did not reuse its own file."
    );
    applyFileCommand(UI::FileCommand::Open, mainPath, ui, views, input);
    require(
        views.activeIndex() == 2 && !views.hasUnsavedChanges(2) && views.activeFile() == mainPath &&
            views.activeScene().getLogicComponent(custom),
        "Open did not replace/clean the active workspace."
    );
    views.select(0, input);
    require(
        views.activeFile() == mainPath && !views.hasUnsavedChanges(0),
        "Other workspace changed Main's path/status."
    );
    views.select(1, input);
    views.activeScene().handleClick(0);
    performFileCommand(UI::FileCommand::Save, nullptr, ui, views, input);
    require(!views.hasUnsavedChanges(1), "Saved draft remained unsaved.");
    CircuitViews imported;
    Input importInput;
    imported.select(0, importInput);
    applyFileCommand(UI::FileCommand::LoadSubcircuit, first, ui, imported, importInput);
    require(
        imported.size() == 1 && imported.mainScene().getComponentCatalog().find(identity.id) &&
            imported.activeFile().empty(),
        "Valid import did not populate Custom or modified Main's file association."
    );
    const auto draftPath = directory / "draft.json";
    saveSubcircuit(draftPath, Scene{}, {"subcircuit.empty", 1}, "Empty draft");
    applyFileCommand(UI::FileCommand::LoadSubcircuit, draftPath, ui, imported, importInput);
    require(
        imported.size() == 2 &&
            imported.role(imported.activeIndex()) == CircuitViews::Role::Subcircuit &&
            !imported.mainScene().getComponentCatalog().find("subcircuit.empty") &&
            !imported.hasUnsavedChanges(1),
        "Draft import became a palette entry."
    );
    applyFileCommand(UI::FileCommand::Open, mainPath, ui, imported, importInput);
    require(
        imported.activeFile() == mainPath && !imported.hasUnsavedChanges(0) &&
            imported.activeIndex() == 0 && imported.mainScene().getLogicComponent(custom),
        "Opening Main did not restore embedded subcircuits."
    );
    importInput.setScene(nullptr);
    input.setScene(nullptr);
    for (const auto& path : {first, second, mainPath, draftPath, workspacePath})
        std::filesystem::remove(path);
    std::filesystem::remove(directory);
}

void uiWorkflow()
{
    Input input;
    CircuitViews views;
    views.select(0, input);
    const auto custom = definition();
    views.publishSubcircuits({custom});
    UI ui;
    FontMetrics font;
    for (auto& character : font.cdata)
        character.xadvance = 16;
    ui.setCircuitViews(&views, &font);
    const CanvasSurface surface{800, 600, 800, 600};
    auto layout = [&] { ui.layout(views.activeScene().getComponentCatalog(), surface, input); };
    auto frame = [&]
    {
        CanvasCamera camera;
        camera.setViewport(input.getCanvasViewport());
        return camera.frame(surface);
    };
    auto event = [&](double x, double y, int button, int action)
    {
        UiInputEvent value{UiInputKind::MouseButton};
        value.code = button;
        value.action = action;
        value.x = x;
        value.y = y;
        ui.handleInput(value, views.activeScene(), input, frame());
        layout();
    };
    auto click = [&](CanvasViewport bounds, int button = GLFW_MOUSE_BUTTON_LEFT)
    {
        event(bounds.x + bounds.width / 2, bounds.y + bounds.height / 2, button, GLFW_PRESS);
        event(bounds.x + bounds.width / 2, bounds.y + bounds.height / 2, button, GLFW_RELEASE);
    };
    layout();
    require(
        ui.fileOptions().size() == 4 && ui.fileOptions()[3] == "Load subcircuit",
        "Workspace menu lacks import."
    );
    click(ui.fileBounds());
    click(ui.fileOptionBounds(3));
    require(
        ui.takeFileCommand() == UI::FileCommand::LoadSubcircuit,
        "Import menu did not queue its command."
    );
    click(ui.tabBounds(UI::Tab::Custom));
    require(
        ui.buttons().size() == 1 && ui.buttons()[0].inputCount == 1 &&
            ui.buttons()[0].outputCount == 1,
        "Published component did not populate Custom with pin counts."
    );
    const auto card = ui.buttons()[0].bounds;
    event(card.x + 10, card.y + 10, GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS);
    const auto center = *frame().worldToWindow({0, 0});
    ui.handleInput(
        {UiInputKind::Cursor, 0, 0, 0, 0, center.x, center.y}, views.activeScene(), input, frame()
    );
    event(center.x, center.y, GLFW_MOUSE_BUTTON_LEFT, GLFW_RELEASE);
    require(
        views.mainScene().getComponentCount() == 1 &&
            views.mainScene().getCommittedComponentView(0)->getDefinitionIdentity().id ==
                custom.identity.id,
        "Custom palette drag did not create a subcircuit."
    );
    click({center.x - 1, center.y - 1, 2, 2}, GLFW_MOUSE_BUTTON_RIGHT);
    require(
        ui.editSubcircuitBounds(views.mainScene()).width > 0,
        "Placed subcircuit popup has no Edit button."
    );
    click(ui.editSubcircuitBounds(views.mainScene()));
    require(
        ui.takeEditSubcircuit() == custom.identity.id && !ui.takeEditSubcircuit(),
        "Edit button did not queue one definition."
    );
    views.editSubcircuit(custom, input);
    layout();
    require(
        ui.fileOptions() == std::vector<std::string>{"Save (subcircuit)", "Save As (subcircuit)"} &&
            ui.fileOptionBounds(2).width == 0,
        "Subcircuit menu retained workspace commands."
    );
    click(ui.fileBounds());
    click(ui.fileOptionBounds(0));
    require(
        ui.takeFileCommand() == UI::FileCommand::Save,
        "Contextual subcircuit Save did not queue its command."
    );
    ui.setCircuitViews(nullptr, nullptr);
    input.setScene(nullptr);
}

} // namespace

int main(int argc, char** argv)
{
    try
    {
        const std::string test = argc > 1 ? argv[1] : "";
        if (test == "subcircuit_runtime")
            runtime();
        else if (test == "subcircuit_clocks")
            clocks();
        else if (test == "subcircuit_files")
            files();
        else if (test == "subcircuit_editing")
            editing();
        else if (test == "subcircuit_file_commands")
            fileCommands();
        else if (test == "subcircuit_ui")
            uiWorkflow();
        else
            throw std::runtime_error("Unknown test");
        std::cout << "PASS: " << test << '\n';
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
