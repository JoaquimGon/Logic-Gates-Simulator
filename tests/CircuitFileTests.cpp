#include "Components/Definitions/NativeDefinitions.h"
#include "Editor/Actions/EditorActions.h"
#include "Persistence/CircuitFile.h"

#include <chrono>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace
{
using Json = nlohmann::json;

void require(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}

void edit(Scene& scene, const EditBatch& batch)
{
    const auto result = EditorActions(scene).apply(batch);
    if (!result)
        throw std::runtime_error(result.message);
}

Scene fixture()
{
    Scene scene;
    int index = 0;
    for (const auto& definition : nativeDefinitions())
        scene.addComponent(definition.identity.id, {30 * index++, 0});
    int input = -1, gate = -1, clock = -1, latch = -1, output = -1;
    for (const auto& [id, view] : scene.getComponentViewMap())
    {
        const auto& name = view->getDefinitionIdentity().id;
        if (name == BuiltinComponentIds::Input)
            input = id;
        if (name == BuiltinComponentIds::And)
            gate = id;
        if (name == BuiltinComponentIds::Clock)
            clock = id;
        if (name == BuiltinComponentIds::SrLatch)
            latch = id;
        if (name == BuiltinComponentIds::Output)
            output = id;
    }
    edit(
        scene,
        {ConfigureInput{input, true},
         ConfigureComponentProperties{
             .componentId = input, .label = "Data \"A\"", .labelVisible = false
         },
         ConfigureComponentProperties{
             .componentId = output, .label = "Result", .labelVisible = false
         },
         ConfigureComponentProperties{
             .componentId = gate,
             .label = "Eight inputs",
             .inputCount = 8,
             .inverted = true,
             .inputInversion = ConfigureGateInput{7, true},
             .orientation = Orientation::Up
         },
         ConfigureComponentProperties{.componentId = latch, .orientation = Orientation::Down},
         ConfigureClock{clock, 2.5f, true},
         ConfigureComponentProperties{
             .componentId = clock, .label = "Reference", .labelVisible = true
         }}
    );

    auto custom = *scene.getComponentCatalog().find(BuiltinComponentIds::DLatch);
    custom.identity = {"example.memory", 3};
    custom.displayName = "Box memory";
    custom.presentation.showBodyLabel = false;
    custom.presentation.kind = PresentationKind::Box;
    custom.presentation.shader = boxShaderResources();
    custom.presentation.body = {BodyContour::Box, false, {0.3f, 0.5f, 0.7f, 1}};
    custom.layout.pins.front().lead = {{-2, 1}, custom.layout.pins.front().anchor};
    edit(
        scene,
        {RegisterComponentDefinition{custom},
         CreateComponent{
             .definitionId = custom.identity.id,
             .position = {600, 0},
             .orientation = Orientation::Left
         }}
    );
    auto pin = [&](int id, bool outgoing, unsigned int pinIndex)
    {
        const auto* view = scene.getCommittedComponentView(id);
        const auto& pins = outgoing ? view->getOutputPins() : view->getInputPins();
        for (const auto& p : pins)
            if (p.pin_index == pinIndex)
                return view->getAbsolutePinGridPos(p);
        throw std::runtime_error("Fixture pin is missing.");
    };
    const auto a = pin(input, true, 0), b = pin(output, false, 0);
    edit(scene, {AddWire{{a, {a.x, -30}, {b.x, -30}, b}}});
    for (int i = 0; i < 2; ++i)
    {
        const int sink = scene.addComponent(BuiltinComponentIds::Output, {500 + 30 * i, 0});
        const auto from = pin(latch, true, static_cast<unsigned int>(i)), to = pin(sink, false, 0);
        // The rotated outputs face down; leave separately before routing to the sinks.
        const int y = i == 0 ? -10 : -20;
        edit(scene, {AddWire{{from, {from.x, y}, {to.x, y}, to}}});
    }
    scene.propagate();
    scene.syncVisuals();
    return scene;
}

void roundtrip()
{
    auto scene = fixture();
    const auto text = circuitToJson(scene, "My circuit");
    auto loaded = circuitFromJson(text);
    require(
        loaded.name == "My circuit" && circuitToJson(loaded.scene, loaded.name) == text,
        "Circuit design changed during its JSON round trip."
    );
    auto legacy = Json::parse(text);
    for (auto& component : legacy["components"])
    {
        component.erase("label_visible");
        component.erase("orientation");
    }
    for (auto& definition : legacy["definitions"])
        definition.erase("show_body_label");
    for (auto& component : legacy["components"])
        for (auto& pin : component["layout"]["pins"])
        {
            pin.erase("inverted");
            if (pin["direction"] == "input" && pin["id"].get<std::string>().starts_with("in.") &&
                pin["index"].get<int>() >= 2)
                pin["label"] = "In " + std::to_string(pin["index"].get<int>());
        }
    for (auto& definition : legacy["definitions"])
        for (auto& pin : definition["layout"]["pins"])
            pin.erase("inverted");
    auto old = circuitFromJson(legacy.dump());
    for (const auto& [id, view] : old.scene.getComponentViewMap())
        if (const auto* gate = dynamic_cast<const Gate*>(old.scene.getLogicComponent(id)))
            for (int pin = 0; pin < gate->getInputPinCount(); ++pin)
            {
                require(
                    view->getOrientation() == Orientation::Right,
                    "Old saves changed gate orientation."
                );
                require(!gate->isInputInverted(pin), "Old saves inverted inputs by default.");
                require(
                    view->getInputPins()[pin].label == std::string(1, static_cast<char>('A' + pin)),
                    "Legacy gate input names were not converted to alphabetic names."
                );
            }
    for (const auto& [id, view] : old.scene.getComponentViewMap())
        require(
            view->showsBodyLabel() == !dynamic_cast<const Clock*>(old.scene.getLogicComponent(id)),
            "Old saves did not retain the existing clock/other label defaults."
        );
    require(
        loaded.scene.getLastEvalResult() == SimulationResult::OK &&
            loaded.scene.getShortedNetCount() == 0,
        "Round trip changed circuit connectivity."
    );
    int previewId = -1;
    for (const auto& [id, view] : loaded.scene.getComponentViewMap())
    {
        const auto* logic = loaded.scene.getLogicComponent(id);
        if (const auto* gate = dynamic_cast<const Gate*>(logic);
            gate && gate->getInputPinCount() == 8)
            require(
                gate->getType() == NAND && view->getBodyStyle().inverted &&
                    view->getOrientation() == Orientation::Up && gate->isInputInverted(7) &&
                    !gate->isInputInverted(0) && view->getInputPins()[7].inverted &&
                    view->getInputPins()[0].relative_pos.x == -3,
                "Edited gate arity/inversion was not reconstructed."
            );
        if (view->getDefinitionIdentity().id == BuiltinComponentIds::SrLatch)
            require(
                view->getOrientation() == Orientation::Down, "Native latch orientation was lost."
            );
        if (view->getDefinitionIdentity().id == "example.memory")
            require(
                view->getOrientation() == Orientation::Left, "Custom latch orientation was lost."
            );
        if (const auto* clock = dynamic_cast<const Clock*>(logic))
            require(
                clock->getFrequency() == 2.5f && clock->isPaused(), "Clock settings were lost."
            );
        if (const auto* input = dynamic_cast<const InputPin*>(logic))
        {
            require(
                input->getState() && view->getBodyLabel() == "Data \"A\"",
                "Input state/name was lost."
            );
            previewId = id;
        }
        if (view->getDefinitionIdentity().id == BuiltinComponentIds::Output)
        {
            if (view->getGridPosition().x == 500)
                require(!logic->getStateInPin(0), "Q output was not independently reconnected.");
            if (view->getGridPosition().x == 530)
                require(
                    logic->getStateInPin(0), "Inverted Q output was not independently reconnected."
                );
        }
    }
    const auto before = circuitToJson(loaded.scene, loaded.name);
    EditorActions actions(loaded.scene);
    auto preview = actions.beginMove(previewId);
    require(preview && actions.previewMove(*preview, {-200, 10}), "Preview fixture failed.");
    require(
        circuitToJson(loaded.scene, loaded.name) == before,
        "Save included uncommitted preview geometry."
    );
    actions.cancelMove(*preview);
    Scene loop;
    const int inverter = loop.addComponent(BuiltinComponentIds::Not, {0, 0});
    const auto* view = loop.getCommittedComponentView(inverter);
    const auto out = view->getAbsolutePinGridPos(view->getOutputPins().front());
    const auto in = view->getAbsolutePinGridPos(view->getInputPins().front());
    edit(loop, {AddWire{{out, {out.x + 2, out.y}, {out.x + 2, 5}, {in.x, 5}, in}}});
    auto feedback = circuitFromJson(circuitToJson(loop, "Feedback"));
    require(
        feedback.scene.getLastEvalResult() == SimulationResult::NON_CONVERGENT &&
            feedback.scene.wireCount() > 0,
        "Feedback wiring was rejected or lost on load."
    );
    require(
        circuitFromJson(circuitToJson(Scene{}, "Empty")).scene.getComponentCount() == 0,
        "Empty circuits cannot round trip."
    );
    Scene runtime;
    const int clockId = runtime.addComponent(BuiltinComponentIds::Clock, {0, 0});
    const int latchId = runtime.addComponent(BuiltinComponentIds::SrLatch, {20, 0});
    auto* oldClock = dynamic_cast<Clock*>(runtime.getLogicComponent(clockId));
    oldClock->advanceSlice(0.1);
    oldClock->step();
    auto* oldLatch = runtime.getLogicComponent(latchId);
    oldLatch->setStateInPin(0, true);
    oldLatch->evaluate();
    require(oldClock->getStateOutPin(0) && oldLatch->getStateOutPin(0), "Runtime fixture failed.");
    auto reset = circuitFromJson(circuitToJson(runtime, "Runtime reset"));
    auto* resetClock = dynamic_cast<Clock*>(reset.scene.getLogicComponent(clockId));
    require(
        resetClock->isPaused() && !resetClock->getStateOutPin(0) &&
            !reset.scene.getLogicComponent(latchId)->getStateOutPin(0),
        "Circuit design load restored transient clock/latch outputs."
    );
    resetClock->setPaused(false);
    require(resetClock->timeUntilEdge() == 0.5, "Clock phase was not reset on design load.");
}

void validation()
{
    const auto valid = Json::parse(circuitToJson(fixture(), "Validation"));
    auto rejects = [&](Json value)
    {
        bool rejected = false;
        try
        {
            circuitFromJson(value.dump());
        }
        catch (const std::exception&)
        {
            rejected = true;
        }
        require(rejected, "Invalid circuit JSON was accepted.");
    };
    auto value = valid;
    value["version"] = 2;
    rejects(value);
    value = valid;
    value["version"] = 1.5;
    rejects(value);
    value = valid;
    value["components"][0]["definition"] = "missing";
    rejects(value);
    value = valid;
    value["components"][0]["version"] = 99;
    rejects(value);
    value = valid;
    value["components"][0]["position"][0] = 4294967296ULL;
    rejects(value);
    value = valid;
    value["components"][0]["position"][0] = 0.5;
    rejects(value);
    value = valid;
    value["components"][0]["layout"]["size"][0] = -1;
    rejects(value);
    value = valid;
    value["components"][0]["layout"]["pins"][0]["index"] = 99;
    rejects(value);
    value = valid;
    value["wires"] = Json::array({Json::array({{0, 0}, {1, 1}})});
    rejects(value);
    value = valid;
    value["wires"] = Json::object();
    rejects(value);
    value = valid;
    value["definitions"][0]["id"] = "native.override";
    rejects(value);
    value = valid;
    value["definitions"].push_back(value["definitions"][0]);
    rejects(value);
    for (std::size_t i = 0; i < valid["components"].size(); ++i)
        if (valid["components"][i].contains("clock_frequency"))
        {
            value = valid;
            value["components"][i]["clock_frequency"] = 0;
            rejects(value);
            value = valid;
            value["components"][i]["clock_paused"] = "false";
            rejects(value);
        }
    bool rejected = false;
    try
    {
        circuitFromJson("{broken");
    }
    catch (const std::exception&)
    {
        rejected = true;
    }
    require(rejected, "Malformed JSON was accepted.");
}

void fileIo()
{
    auto directory = std::filesystem::temp_directory_path() /
                     ("logic-circuit-test-" +
                      std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directory(directory);
    const auto path = directory / std::filesystem::path{u8"circuit-\u00e7.json"};
    auto cleanup = [&]
    {
        std::error_code ignored;
        std::filesystem::remove(path, ignored);
        std::filesystem::remove(directory, ignored);
    };
    try
    {
        auto scene = fixture();
        saveCircuit(path, scene, "First");
        auto loaded = loadCircuit(path);
        require(
            circuitToJson(loaded.scene, loaded.name) == circuitToJson(scene, "First"),
            "File IO changed the circuit."
        );
        saveCircuit(path, Scene{}, "Second");
        require(loadCircuit(path).name == "Second", "Save did not replace an existing file.");
        bool failed = false;
        try
        {
            saveCircuit(path, Scene{}, std::string(1, static_cast<char>(0xff)));
        }
        catch (const std::exception&)
        {
            failed = true;
        }
        require(
            failed && loadCircuit(path).name == "Second",
            "Failed serialization damaged the previous save."
        );
        failed = false;
        try
        {
            saveCircuit(directory, Scene{}, "Invalid target");
        }
        catch (const std::exception&)
        {
            failed = true;
        }
        require(
            failed && std::distance(
                          std::filesystem::directory_iterator(directory),
                          std::filesystem::directory_iterator{}
                      ) == 1,
            "Failed replacement retained its temporary file."
        );
        std::ofstream(path) << "{malformed";
        failed = false;
        try
        {
            loadCircuit(path);
        }
        catch (const std::exception&)
        {
            failed = true;
        }
        require(failed, "Opening a corrupt save did not report an error.");
        cleanup();
    }
    catch (...)
    {
        cleanup();
        throw;
    }
}
} // namespace

int main(int argc, char** argv)
{
    try
    {
        const std::string test = argc == 2 ? argv[1] : "";
        if (test == "circuit_json_roundtrip")
            roundtrip();
        else if (test == "circuit_json_validation")
            validation();
        else if (test == "circuit_file_io")
            fileIo();
        else
            throw std::runtime_error("Unknown circuit-file test.");
        std::cout << "PASS: " << test << '\n';
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
