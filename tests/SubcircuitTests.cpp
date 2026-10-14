#include "App/CircuitFiles.h"
#include "App/UnsavedWork.h"
#include "Components/Definitions/NativeDefinitions.h"
#include "Editor/Actions/EditorActions.h"
#include "Editor/CircuitViews.h"
#include "Editor/Clipboard.h"
#include "Editor/Input.h"
#include "Editor/Subcircuits.h"
#include "Graphics/Text/TextGeometry.h"
#include "Persistence/CircuitFile.h"
#include "Persistence/SubcircuitLibrary.h"
#include "Simulation/Subcircuit.h"
#include "UI/UI.h"

#include <GLFW/glfw3.h>
#include <chrono>
#include <filesystem>
#include <fstream>
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
    auto legacy = nlohmann::json::parse(text);
    for (auto& component : legacy["design"]["components"])
        component.erase("port_id");
    auto legacyDesign = subcircuitFromJson(legacy.dump());
    const auto legacyDefinition =
        makeSubcircuit(legacyDesign.design.scene, legacyDesign.identity, legacyDesign.design.name);
    require(
        legacyDefinition.layout.pins.front().id == "in.0" &&
            legacyDefinition.layout.pins.back().id == "out.0",
        "Older subcircuit files lost their original port identities."
    );
    Scene legacyParent;
    edit(
        legacyParent,
        {RegisterComponentDefinition{legacyDefinition},
         CreateComponent{legacyDefinition.identity.id, {0, 0}}}
    );
    auto legacyWorkspace = nlohmann::json::parse(circuitToJson(legacyParent, "Legacy"));
    for (auto& component : legacyWorkspace["definitions"][0]["subcircuit"]["design"]["components"])
        component.erase("port_id");
    require(
        circuitFromJson(legacyWorkspace.dump()).scene.getComponentCount() == 1,
        "An older workspace's embedded subcircuit failed to reopen."
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

void orientation()
{
    auto authored = wireDesign(true);
    edit(
        authored, {ConfigureComponentProperties{.componentId = 3, .orientation = Orientation::Up}}
    );
    const auto design = makeSubcircuit(authored, {"subcircuit.rotated", 1}, "Inverter");
    const auto original =
        circuitToJson(*std::get<SubcircuitBehavior>(design.behavior).authored, "Design");
    Scene scene;
    edit(scene, {RegisterComponentDefinition{design}, CreateComponent{design.identity.id, {0, 0}}});
    const int instance = 0;
    const int source =
        scene.addComponent(BuiltinComponentIds::Input, {-20, 0}, {.inputState = true});
    const int sink = scene.addComponent(BuiltinComponentIds::Output, {20, 0});
    edit(scene, {AddWire{{{-19, 0}, {-4, 0}}}, AddWire{{{4, 0}, {19, 0}}}});
    EditorActions actions(scene);
    for (const auto direction :
         {Orientation::Up, Orientation::Down, Orientation::Left, Orientation::Right})
    {
        const auto previous = scene.getCommittedComponentView(instance)->getOrientation();
        const auto result = actions.apply(
            {ConfigureComponentProperties{.componentId = instance, .orientation = direction}}
        );
        require(static_cast<bool>(result), "Subcircuit rotation failed.");
        scene.propagate();
        const auto* view = scene.getCommittedComponentView(instance);
        require(
            view->getAbsolutePinGridPos(view->getInputPins()[0]) ==
                    rotateGridPoint(GridCoords{-4, 0}, direction) &&
                view->getAbsolutePinGridPos(view->getOutputPins()[0]) ==
                    rotateGridPoint(GridCoords{4, 0}, direction) &&
                scene.netOfPin({source, 0}, PinType::OUTPUT) ==
                    scene.netOfPin({instance, 0}, PinType::INPUT) &&
                scene.netOfPin({instance, 0}, PinType::OUTPUT) ==
                    scene.netOfPin({sink, 0}, PinType::INPUT) &&
                !scene.getLogicComponent(sink)->getStateInPin(0),
            "Subcircuit rotation lost anchors, connectivity or indexed behavior."
        );
        scene.handleClick(source);
        scene.propagate();
        require(
            scene.getLogicComponent(sink)->getStateInPin(0),
            "Rotated subcircuit ignored input changes."
        );
        scene.handleClick(source);
        scene.propagate();
        require(
            static_cast<bool>(actions.restore(*result.change->before, scene.getRevision())) &&
                scene.getCommittedComponentView(instance)->getOrientation() == previous,
            "Undo lost subcircuit orientation."
        );
        require(
            static_cast<bool>(actions.restore(*result.change->after, scene.getRevision())) &&
                scene.getCommittedComponentView(instance)->getOrientation() == direction,
            "Redo lost subcircuit orientation."
        );
        const auto json = circuitToJson(scene, "Main");
        const auto loaded = circuitFromJson(json);
        require(
            loaded.scene.getCommittedComponentView(0)->getOrientation() == direction &&
                circuitToJson(loaded.scene, "Main") == json,
            "Saved subcircuit instance lost its orientation or wire routes."
        );
        const auto& behavior = std::get<SubcircuitBehavior>(
            loaded.scene.getComponentCatalog().find(design.identity.id)->behavior
        );
        require(
            circuitToJson(*behavior.authored, "Design") == original,
            "Rotating an instance changed the authored internal layout."
        );
        Selection selection;
        selection.selectComponent(instance);
        EditorClipboard clipboard;
        require(clipboard.copy(scene, selection), "Subcircuit copy failed.");
        Scene pasted;
        edit(pasted, clipboard.pasteBatch(pasted, {0, 0}));
        require(
            pasted.getCommittedComponentView(0)->getOrientation() == direction,
            "Subcircuit clipboard lost its orientation."
        );
    }
}

void interfaceUpdates()
{
    Input input;
    CircuitViews views;
    views.select(0, input);
    auto authored = wireDesign();
    auto original = makeSubcircuit(authored, {"subcircuit.interface", 1}, "Wire");
    views.publishSubcircuits({original});
    auto& parent = views.mainScene();
    const int box = parent.addComponent(original.identity.id, {0, 0});
    const int source = parent.addComponent(BuiltinComponentIds::Input, {-12, 0});
    const int sink = parent.addComponent(BuiltinComponentIds::Output, {12, 0});
    edit(parent, {AddWire{{{-11, 0}, {-4, 0}}}, AddWire{{{4, 0}, {11, 0}}}});
    parent.handleClick(source);
    parent.propagate();
    require(parent.getLogicComponent(sink)->getStateInPin(0), "Initial interface wiring failed.");
    const auto portId = authored.getCommittedComponentView(1)->getPortId();
    edit(authored, {ConfigureComponentProperties{.componentId = 1, .label = "Renamed"}});
    const int extra = authored.addComponent(BuiltinComponentIds::Input, {-30, 10});
    auto publish = [&](unsigned version)
    {
        // Save reloads the authored scene; identity must survive its component-ID compaction.
        auto saved =
            subcircuitFromJson(subcircuitToJson(authored, {original.identity.id, version}, "Wire"));
        views.publishSubcircuits({makeSubcircuit(saved.design.scene, saved.identity, "Wire")});
        parent.propagate();
    };
    // Import the intentionally colliding fixture without routing its existing port wire.
    const int blocker =
        parent.addComponent(BuiltinComponentIds::Input, {-5, 1}, {}, PlacementPolicy::AllowOverlap);
    const auto beforeRejected = circuitToJson(parent, "Main");
    rejects([&] { publish(2); }, "A colliding updated pin was published.");
    require(
        circuitToJson(parent, "Main") == beforeRejected &&
            parent.getComponentCatalog().find(original.identity.id)->identity.version == 1,
        "Failed interface migration partially updated the parent."
    );
    parent.removeComponent(blocker);
    publish(2);
    const auto* view = parent.getCommittedComponentView(box);
    require(
        view->getInputPins().size() == 2 && view->getInputPins()[0].label == "Renamed" &&
            parent.netOfPin({box, 0}, PinType::INPUT) ==
                parent.netOfPin({source, 0}, PinType::OUTPUT) &&
            parent.netOfPin({box, 1}, PinType::INPUT) == INVALID_NET_ID &&
            parent.netOfPin({box, 0}, PinType::OUTPUT) ==
                parent.netOfPin({sink, 0}, PinType::INPUT) &&
            parent.getLogicComponent(sink)->getStateInPin(0),
        "Rename/expansion lost surviving wires or connected a new port."
    );
    const auto roundTrip = circuitFromJson(circuitToJson(parent, "Main"));
    const auto& stored = std::get<SubcircuitBehavior>(
        roundTrip.scene.getComponentCatalog().find(original.identity.id)->behavior
    );
    require(
        stored.authored->getCommittedComponentView(0)->getPortId() == portId,
        "Saving/reopening discarded stable authored port identity."
    );

    const int secondSource = parent.addComponent(BuiltinComponentIds::Input, {-12, -4});
    edit(parent, {AddWire{{{-11, -4}, {-8, -4}, {-8, -1}, {-4, -1}}}});
    parent.handleClick(secondSource);
    parent.propagate();
    // Removing the first port shifts the next port to index zero. Its old wire must not be
    // inherited.
    authored.removeComponent(1);
    const int newInput = authored.addComponent(BuiltinComponentIds::Input, {-40, 15});
    edit(authored, {ConfigureComponentProperties{.componentId = newInput, .label = "Renamed"}});
    const int newOutput = authored.addComponent(BuiltinComponentIds::Output, {35, 15});
    edit(authored, {AddWire{{{-39, 15}, {-39, 20}, {34, 20}, {34, 15}}}});
    publish(3);
    require(
        parent.getLogicComponent(box)->getInputPinCount() == 2 &&
            parent.netOfPin({box, 0}, PinType::INPUT) ==
                parent.netOfPin({secondSource, 0}, PinType::OUTPUT) &&
            parent.netOfPin({box, 0}, PinType::INPUT) !=
                parent.netOfPin({source, 0}, PinType::OUTPUT) &&
            parent.getLogicComponent(box)->getStateInPin(0) &&
            parent.netOfPin({box, 1}, PinType::INPUT) == INVALID_NET_ID &&
            parent.netOfPin({box, 0}, PinType::OUTPUT) ==
                parent.netOfPin({sink, 0}, PinType::INPUT) &&
            !parent.getLogicComponent(sink)->getStateInPin(0) && parent.wireCount() >= 2,
        "Deleting/replacing a port silently transferred its wire to another input."
    );
    require(
        authored.getCommittedComponentView(extra)->getPortId() !=
            authored.getCommittedComponentView(newInput)->getPortId(),
        "New input reused an existing port identity."
    );

    // Rotated instances use the same identity mapping in their own coordinate frame.
    edit(
        parent, {ConfigureComponentProperties{.componentId = box, .orientation = Orientation::Up}}
    );
    authored.removeComponent(newOutput);
    require(views.inspectComponent(box, input), "Cannot open the instance before publication.");
    publish(4);
    require(
        !views.refreshInspection(input) && !views.inspecting() && !input.isReadOnly(),
        "Publishing an interface change left the live viewer pointing to stale runtime."
    );
    require(
        parent.getCommittedComponentView(box)->getOrientation() == Orientation::Up &&
            parent.netOfPin({box, 0}, PinType::OUTPUT) ==
                parent.netOfPin({sink, 0}, PinType::INPUT),
        "Shrinking a rotated interface lost surviving output wiring."
    );
    authored.removeComponent(2);
    const int replacementOutput = authored.addComponent(BuiltinComponentIds::Output, {20, 0});
    edit(
        authored,
        {ConfigureComponentProperties{.componentId = replacementOutput, .label = "Result"}}
    );
    publish(5);
    require(
        parent.netOfPin({box, 0}, PinType::OUTPUT) == INVALID_NET_ID &&
            !parent.getLogicComponent(sink)->getStateInPin(0) && parent.getShortedNetCount() == 0 &&
            parent.getLastEvalResult() == SimulationResult::OK,
        "A recreated output inherited the removed output's wire or broke simulation."
    );
    input.setScene(nullptr);
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
    auto expanded = *std::get<SubcircuitBehavior>(replacement.behavior).authored;
    const int extra = expanded.addComponent(BuiltinComponentIds::Input, {-40, 10});
    edit(expanded, {ConfigureComponentProperties{.componentId = extra, .label = "Extra"}});
    auto changed = makeSubcircuit(expanded, {"subcircuit.test", 3}, "Changed");
    views.publishSubcircuits({changed});
    require(
        views.mainScene().getLogicComponent(id)->getInputPinCount() == 2 &&
            views.mainScene().getCommittedComponentView(id)->getSize() ==
                glm::vec2(changed.layout.width, changed.layout.height),
        "Interface expansion did not update the placed model and box."
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
        ("logic-library-commands-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    SubcircuitLibrary library(directory / "profile");
    Input input;
    CircuitViews views;
    views.select(0, input);
    UI ui;
    views.create(input, CircuitViews::Role::Subcircuit);
    views.activeScene() = wireDesign();
    views.rename(1, "Reusable");
    require(
        !applyFileCommand(
            UI::FileCommand::ExportSubcircuit, std::nullopt, ui, views, input, &library
        ) && views.activeDefinition().id.empty() &&
            views.hasUnsavedChanges(1),
        "Cancelled export modified the editor."
    );
    require(
        performFileCommand(UI::FileCommand::Save, nullptr, ui, views, input, &library),
        "Managed save required a picker."
    );
    const auto identity = views.activeDefinition();
    const auto publishedPath = views.activeFile();
    require(
        !views.hasUnsavedChanges(1) && views.mainScene().getComponentCatalog().find(identity.id) &&
            publishedPath.parent_path() == library.location() / "library",
        "Save did not publish and remember the managed design."
    );
    views.rename(1, "Renamed");
    performFileCommand(UI::FileCommand::Save, nullptr, ui, views, input, &library);
    require(
        loadSubcircuit(publishedPath).design.name == "Renamed", "Save changed the identity's path."
    );
    views.rename(1, "Unpublished edit");
    const auto exported = directory / "portable.json";
    applyFileCommand(UI::FileCommand::ExportSubcircuit, exported, ui, views, input, &library);
    require(
        views.activeFile() == publishedPath && views.hasUnsavedChanges(1) &&
            loadSubcircuit(exported).design.name == "Unpublished edit",
        "Export changed the Save destination or cleared unsaved work."
    );
    const int custom = views.mainScene().addComponent(identity.id, {0, 0});
    // An actually unfinished design still saves as a draft without replacing usable instances.
    views.activeScene().removeComponent(1);
    const auto oldVersion = loadSubcircuit(publishedPath).identity.version;
    performFileCommand(UI::FileCommand::Save, nullptr, ui, views, input, &library);
    const auto draftPath = views.activeFile();
    require(
        !views.hasUnsavedChanges(1) && draftPath.parent_path() == library.location() / "drafts" &&
            loadSubcircuit(publishedPath).identity.version == oldVersion &&
            views.mainScene().getLogicComponent(custom)->getInputPinCount() == 1,
        "Incomplete draft overwrote the usable published version."
    );
    views.select(0, input);
    const auto mainPath = directory / "main.json";
    applyFileCommand(UI::FileCommand::SaveAs, mainPath, ui, views, input, &library);
    views.create(input);
    views.activeScene().addComponent(BuiltinComponentIds::Not, {0, 0});
    const auto workspacePath = directory / "workspace.json";
    bool failedSave = false;
    try
    {
        applyFileCommand(
            UI::FileCommand::SaveAs,
            directory / "missing" / "failure.json",
            ui,
            views,
            input,
            nullptr
        );
    }
    catch (const std::exception&)
    {
        failedSave = true;
    }
    require(
        failedSave && views.hasUnsavedChanges(2) && views.activeFile().empty(),
        "Failed workspace Save changed its association or cleared pending work."
    );
    applyFileCommand(UI::FileCommand::SaveAs, workspacePath, ui, views, input, nullptr);
    views.rename(2, "Workspace edit");
    performFileCommand(UI::FileCommand::Save, nullptr, ui, views, input, &library);
    require(
        loadCircuit(workspacePath).name == "Workspace edit" &&
            loadCircuit(mainPath).scene.getLogicComponent(custom),
        "Save overwrote another workspace."
    );
    require(
        !applyFileCommand(UI::FileCommand::Open, std::nullopt, ui, views, input, &library) &&
            views.activeFile() == workspacePath,
        "Cancelled Open changed file association."
    );
    applyFileCommand(UI::FileCommand::Open, mainPath, ui, views, input, &library);
    require(
        views.activeIndex() == 2 && views.activeFile() == mainPath && !views.hasUnsavedChanges(2),
        "Open did not replace only the active workspace."
    );
    SubcircuitLibrary importedLibrary(directory / "imported");
    CircuitViews imported;
    Input importInput;
    imported.select(0, importInput);
    applyFileCommand(
        UI::FileCommand::LoadSubcircuit, exported, ui, imported, importInput, &importedLibrary
    );
    std::filesystem::remove(exported);
    SubcircuitLibrary restarted(directory / "imported");
    require(
        restarted.entries().size() == 1 && !restarted.entries()[0].draft,
        "Imported subcircuit depended on the original file."
    );
    const auto invalidFile = directory / "invalid.json";
    saveSubcircuit(invalidFile, Scene{}, {"subcircuit.empty", 1}, "Empty draft");
    applyFileCommand(
        UI::FileCommand::LoadSubcircuit, invalidFile, ui, imported, importInput, &importedLibrary
    );
    require(
        imported.size() == 2 &&
            !imported.mainScene().getComponentCatalog().find("subcircuit.empty") &&
            !imported.hasUnsavedChanges(1),
        "Incomplete import was lost or became placeable."
    );
    importInput.setScene(nullptr);
    input.setScene(nullptr);
    std::filesystem::remove_all(directory);
}

void libraryStorage()
{
    const auto directory =
        std::filesystem::temp_directory_path() /
        ("logic-library-storage-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto profile = directory / "profile";
    SubcircuitLibrary library(profile);
    SavedSubcircuit published{{"subcircuit.storage", 1}, {"Original", wireDesign()}};
    const auto original = library.store(published, false);
    SavedSubcircuit draft{{"subcircuit.storage", 2}, {"Draft edit", Scene{}}};
    library.store(draft, true);
    SubcircuitLibrary restart(profile);
    require(
        restart.entries().size() == 2 && restart.version(published.identity.id) == 2,
        "Published designs/drafts did not survive restart."
    );
    const auto target = directory / "new storage";
    library.changeLocation(target);
    SubcircuitLibrary relocated(profile);
    require(
        relocated.location() == target && relocated.entries().size() == 2 &&
            std::filesystem::exists(original),
        "Relocation lost entries or deleted original designs."
    );
    const auto conflict = directory / "conflicting";
    std::filesystem::create_directories(conflict / "library");
    std::ofstream(conflict / "library" / original.filename()) << "different";
    bool failed = false;
    try
    {
        library.changeLocation(conflict);
    }
    catch (const std::exception&)
    {
        failed = true;
    }
    require(
        failed && SubcircuitLibrary(profile).location() == target,
        "Conflicting destination was overwritten or remembered after failure."
    );
    std::ofstream(target / "library" / "damaged.json") << "{bad json";
    SubcircuitLibrary damaged(profile);
    require(
        damaged.entries().size() == 2 && damaged.errors().size() == 1,
        "One damaged library file hid valid designs/drafts."
    );
    CircuitViews views;
    Input input;
    UI ui;
    views.select(0, input);
    refreshSubcircuitLibrary(damaged, views, ui);
    require(
        views.mainScene().getComponentCatalog().find(published.identity.id),
        "Startup library was not available in Custom."
    );
    auto embedded = makeSubcircuit(wireDesign(true), {published.identity.id, 7}, "Embedded");
    Scene workspace;
    edit(
        workspace,
        {RegisterComponentDefinition{embedded}, CreateComponent{embedded.identity.id, {0, 0}}}
    );
    views.create(input);
    views.replaceWorkspace(std::move(workspace), "Project", input);
    require(
        views.mainScene().getComponentCatalog().find(published.identity.id)->identity.version ==
                1 &&
            views.activeScene()
                    .getComponentCatalog()
                    .find(published.identity.id)
                    ->identity.version == 7 &&
            loadSubcircuit(target / "library" / original.filename()).identity.version == 1,
        "Opening a workspace published embedded versions into other scenes/the managed library."
    );
    const auto failedPath = target / "library" / original.filename();
    std::filesystem::remove(failedPath);
    std::filesystem::create_directory(failedPath); // Destination replacement must fail.
    published.identity.version = 3;
    failed = false;
    try
    {
        damaged.store(published, false);
    }
    catch (const std::exception&)
    {
        failed = true;
    }
    require(
        failed && std::filesystem::is_directory(failedPath),
        "Failed Save destroyed its destination."
    );
    input.setScene(nullptr);
    std::filesystem::remove_all(directory);
}

void unsavedWork()
{
    Input input;
    CircuitViews views;
    UI ui;
    views.select(0, input);
    views.markSaved(0);
    views.create(input);
    views.activeScene().addComponent(BuiltinComponentIds::And, {0, 0});
    ui.layout(views.activeScene().getComponentCatalog(), {800, 600, 800, 600}, input);
    UnsavedWork guard;
    auto choose = [&](UI::UnsavedChoice choice)
    {
        const auto bounds = ui.unsavedButtonBounds(choice);
        UiInputEvent event{UiInputKind::MouseButton, GLFW_MOUSE_BUTTON_LEFT, GLFW_RELEASE};
        event.x = bounds.x + bounds.width / 2;
        event.y = bounds.y + bounds.height / 2;
        event.action = GLFW_PRESS;
        ui.handleInput(event, views.activeScene(), input, {});
        event.action = GLFW_RELEASE;
        require(
            ui.handleInput(event, views.activeScene(), input, {}), "Prompt did not capture input."
        );
    };
    int saves = 0;
    auto saved = [&]()
    {
        ++saves;
        views.markSaved(views.activeIndex());
        return true;
    };
    guard.request({UnsavedWork::Operation::Close, 1}, views, ui, input);
    require(ui.unsavedPromptOpen() && input.getUiCapture().pointer, "Dirty close did not ask.");
    UiInputEvent shortcut{UiInputKind::Key, GLFW_KEY_F2, GLFW_PRESS};
    require(
        ui.handleInput(shortcut, views.activeScene(), input, {}), "Modal allowed shortcuts through."
    );
    choose(UI::UnsavedChoice::Cancel);
    require(
        !guard.resolve(views, ui, input, saved) && views.size() == 2 && saves == 0,
        "Cancelled close saved or removed work."
    );
    guard.request({UnsavedWork::Operation::Open, 1}, views, ui, input);
    choose(UI::UnsavedChoice::Save);
    require(
        !guard.resolve(views, ui, input, [] { return false; }) && views.hasUnsavedChanges(1),
        "Cancelled Save picker approved destructive Open."
    );
    guard.request({UnsavedWork::Operation::Exit, 1}, views, ui, input);
    choose(UI::UnsavedChoice::Save);
    require(
        !guard.resolve(
            views, ui, input, []() -> bool { throw std::runtime_error("Write failed"); }
        ) && views.size() == 2 &&
            views.hasUnsavedChanges(1),
        "Failed Save approved exit."
    );
    guard.request({UnsavedWork::Operation::Exit, 1}, views, ui, input);
    choose(UI::UnsavedChoice::Save);
    require(
        guard.resolve(views, ui, input, saved).has_value() && saves == 1 &&
            !views.hasUnsavedChanges(1),
        "Successful Save All did not approve exit."
    );
    views.rename(1, "Changed child");
    guard.request({UnsavedWork::Operation::Exit, 1}, views, ui, input);
    choose(UI::UnsavedChoice::Save);
    int cascadeSaves = 0;
    require(
        guard.resolve(
                 views,
                 ui,
                 input,
                 [&]
                 {
                     ++cascadeSaves;
                     const auto index = views.activeIndex();
                     views.markSaved(index);
                     if (index == 1)
                         views.rename(0, "Updated by child publication");
                     return true;
                 }
        ).has_value() &&
            cascadeSaves == 2 && !views.hasUnsavedChanges(0) && !views.hasUnsavedChanges(1),
        "Save All exited with new unsaved changes published into an initially clean workspace."
    );
    views.rename(0, "Dirty Main");
    views.rename(1, "Dirty Workspace");
    guard.request({UnsavedWork::Operation::Exit, 1}, views, ui, input);
    choose(UI::UnsavedChoice::Save);
    int partial = 0;
    require(
        !guard.resolve(
            views,
            ui,
            input,
            [&]
            {
                if (++partial == 2)
                    return false;
                views.markSaved(views.activeIndex());
                return true;
            }
        ) && views.activeIndex() == 1 &&
            !views.hasUnsavedChanges(0) && views.hasUnsavedChanges(1),
        "Partially cancelled Save All discarded unsaved work or lost active view."
    );
    guard.request({UnsavedWork::Operation::Close, 1}, views, ui, input);
    choose(UI::UnsavedChoice::Discard);
    require(
        guard.resolve(views, ui, input, saved).has_value() && views.hasUnsavedChanges(1),
        "Discard saved work instead of approving its explicit removal."
    );
    guard.request({UnsavedWork::Operation::Close, 0}, views, ui, input);
    require(!guard.pending(), "Main could be closed.");
    input.setScene(nullptr);
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
        ui.fileOptions().size() == 6 && ui.fileOptions()[3] == "Import subcircuit...",
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
    require(
        ui.gateSettingsBounds(views.mainScene()).width == 0,
        "Subcircuit gained gate-only arity/inversion controls."
    );
    input.setMode(EditorMode::Selection);
    layout();
    for (const auto direction :
         {Orientation::Up, Orientation::Down, Orientation::Left, Orientation::Right})
    {
        click(ui.componentOrientationBounds(views.mainScene(), direction));
        require(
            views.mainScene().getCommittedComponentView(0)->getOrientation() == direction,
            "Subcircuit popup did not rotate the instance."
        );
    }
    input.setMode(EditorMode::Interaction);
    layout();
    click(ui.componentOrientationBounds(views.mainScene(), Orientation::Up));
    require(
        views.mainScene().getCommittedComponentView(0)->getOrientation() == Orientation::Right,
        "Interaction mode edited subcircuit orientation."
    );
    input.setMode(EditorMode::Selection);
    layout();
    click(ui.editSubcircuitBounds(views.mainScene()));
    require(
        ui.takeEditSubcircuit() == custom.identity.id && !ui.takeEditSubcircuit(),
        "Edit button did not queue one definition."
    );
    views.editSubcircuit(custom, input);
    layout();
    require(
        ui.fileOptions().size() == 5 && ui.fileOptions()[0] == "Save to library" &&
            ui.fileOptions()[1] == "Export subcircuit...",
        "Subcircuit menu retained workspace commands."
    );
    click(ui.fileBounds());
    click(ui.fileOptionBounds(0));
    require(
        ui.takeFileCommand() == UI::FileCommand::Save,
        "Contextual subcircuit Save did not queue its command."
    );
    ui.dismissPopups(input);
    views.select(0, input);
    ui.setLibraryDrafts({{"subcircuit.incomplete", "Incomplete", {}, 0, 0, "draft.json"}});
    layout();
    click(ui.tabBounds(UI::Tab::Custom));
    layout();
    const auto draftButton = ui.buttons().back();
    click(draftButton.bounds);
    require(
        !ui.dragging() && ui.takeOpenDraft() == std::filesystem::path("draft.json") &&
            !ui.takeOpenDraft(),
        "Saved draft was placeable or did not queue one editor open."
    );
    input.setUiCapture({});
    click(ui.buttons().front().bounds, GLFW_MOUSE_BUTTON_RIGHT);
    require(
        ui.takeEditSubcircuit() == custom.identity.id,
        "Custom row could not reopen authored design."
    );
    input.setUiCapture({});
    views.rename(1, "Unsaved design");
    click(ui.circuitTabs().closeBounds(1));
    require(
        views.size() == 2 && ui.takeCloseCircuit() == 1,
        "Dirty tab was deleted before its Save/Discard/Cancel decision."
    );
    input.setUiCapture({});
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
        else if (test == "subcircuit_orientation")
            orientation();
        else if (test == "subcircuit_editing")
        {
            editing();
            interfaceUpdates();
        }
        else if (test == "subcircuit_file_commands")
            fileCommands();
        else if (test == "subcircuit_library")
            libraryStorage();
        else if (test == "unsaved_work")
            unsavedWork();
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
