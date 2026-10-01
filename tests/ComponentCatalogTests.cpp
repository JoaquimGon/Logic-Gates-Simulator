#include "Components/ComponentFactory.h"
#include "Components/Definitions/NativeDefinitions.h"
#include "Editor/Actions/EditorActions.h"
#include "Editor/Scene.h"

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

namespace
{
void require(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}

EditResult accepted(EditResult result)
{
    if (!result)
        throw std::runtime_error(result.message);
    return result;
}

template <typename F>
void invalid(F action)
{
    try
    {
        action();
    }
    catch (const std::invalid_argument&)
    {
        return;
    }
    throw std::runtime_error("Invalid definition/options were accepted.");
}

ComponentDefinition boxDefinition(const char* id = "custom.buffer")
{
    ComponentDefinition definition;
    definition.identity = {id, 3};
    definition.displayName = "Custom inverter";
    definition.behavior = NOT;
    definition.layout = {
        0.3f,
        0.2f,
        {{"data", "Data", PinType::INPUT, 0, {-3, 0}, {}},
         {"result", "Result", PinType::OUTPUT, 0, {3, 0}, {}}}
    };
    definition.presentation.shader = boxShaderResources();
    definition.presentation.bodyLabel = "CUSTOM";
    definition.presentation.showPinLabels = true;
    return definition;
}

ComponentLayout layoutOf(const ComponentView& view)
{
    return {view.getSize(), view.getShaderName(), view.getInputPins(), view.getOutputPins()};
}

void catalogDefaults()
{
    Scene scene;
    const auto& definitions = scene.getComponentCatalog().definitions();
    require(definitions.size() == 11, "A built-in definition was lost.");
    int x = 0;
    for (const auto& [id, definition] : definitions)
    {
        const auto* original = scene.getComponentCatalog().find(id);
        const int instance = scene.addComponent(id, {x, 0});
        x += 20;
        const auto& view = *scene.getCommittedComponentView(instance);
        require(
            view.getDefinitionIdentity().id == id &&
                view.getDefinitionIdentity().version == definition.identity.version &&
                view.getSize() == glm::vec2(definition.layout.width, definition.layout.height) &&
                view.getShaderName() == definition.presentation.shader.key,
            "Default creation disagrees with the declaration table."
        );
        for (const auto* pins : {&view.getInputPins(), &view.getOutputPins()})
            for (const auto& pin : *pins)
            {
                auto declared = std::find_if(
                    definition.layout.pins.begin(),
                    definition.layout.pins.end(),
                    [&](const auto& value)
                    { return value.direction == pin.type && value.index == pin.pin_index; }
                );
                require(
                    declared != definition.layout.pins.end() && pin.id == declared->id &&
                        pin.label == declared->label && pin.relative_pos == declared->anchor,
                    "View lost a declared pin identity, label, or anchor."
                );
            }
        require(
            scene.getLogicComponent(instance)->getInputPinCount() ==
                    static_cast<int>(view.getInputPins().size()) &&
                scene.getLogicComponent(instance)->getOutputPinCount() ==
                    static_cast<int>(view.getOutputPins().size()),
            "Factory created mismatched logic and visual interfaces."
        );
        require(
            scene.getComponentCatalog().find(id) == original,
            "Ordinary instance creation copied/mutated catalog storage."
        );
        const auto& resources = definition.presentation.shader;
        require(
            std::filesystem::exists(
                std::filesystem::path(CATALOG_ASSETS_DIR) / resources.vertexPath
            ) &&
                std::filesystem::exists(
                    std::filesystem::path(CATALOG_ASSETS_DIR) / resources.fragmentPath
                ),
            "Native descriptor refers to missing rendering assets."
        );
    }
    ComponentOverrides options;
    options.inputCount = 4;
    const int wide = scene.addComponent(BuiltinComponentIds::And, {300, 0}, options);
    require(
        scene.getLogicComponent(wide)->getInputPinCount() == 4 &&
            scene.getCommittedComponentView(wide)->getInputPins()[2].id == "in.2",
        "Arity option did not create coherent stable pins."
    );
    const int normal = scene.addComponent(BuiltinComponentIds::And, {320, 0});
    require(
        scene.getLogicComponent(normal)->getInputPinCount() == 2 &&
            scene.getCommittedComponentView(normal)->getSize().y == 0.2f,
        "One instance's options changed catalog defaults."
    );
    ComponentOverrides clockOptions;
    clockOptions.clockFrequency = 2.0f;
    clockOptions.clockPaused = true;
    const int clock = scene.addComponent(BuiltinComponentIds::Clock, {340, 0}, clockOptions);
    require(
        static_cast<Clock*>(scene.getLogicComponent(clock))->getFrequency() == 2 &&
            static_cast<Clock*>(scene.getLogicComponent(clock))->isPaused(),
        "Clock defaults/options were not applied to logic."
    );
    ComponentOverrides inputOptions;
    inputOptions.inputState = true;
    const int source = scene.addComponent(BuiltinComponentIds::Input, {360, 0}, inputOptions);
    require(scene.getLogicComponent(source)->getStateOutPin(), "Input state override was ignored.");
}

void definitionValidation()
{
    ComponentCatalog catalog;
    auto definition = boxDefinition();
    catalog.registerDefinition(definition);
    const auto count = catalog.definitions().size();
    invalid([&] { catalog.registerDefinition(definition); });
    auto malformed = definition;
    malformed.identity.id = BuiltinComponentIds::And;
    invalid([&] { catalog.registerDefinition(malformed); });
    malformed = boxDefinition("custom.invalid");
    malformed.identity.version = 0;
    invalid([&] { catalog.registerDefinition(malformed); });
    malformed = boxDefinition("custom.invalid");
    malformed.layout.pins[1].id = "data";
    invalid([&] { catalog.registerDefinition(malformed); });
    malformed = boxDefinition("custom.invalid");
    malformed.layout.pins[1].index = 2;
    invalid([&] { catalog.registerDefinition(malformed); });
    malformed = boxDefinition("custom.invalid");
    malformed.layout.pins[1].anchor = {-3, 0};
    invalid([&] { catalog.registerDefinition(malformed); });
    malformed = boxDefinition("custom.invalid");
    malformed.behavior = AND;
    invalid([&] { catalog.registerDefinition(malformed); });
    malformed = boxDefinition("custom.invalid");
    malformed.behavior = static_cast<GateType>(999);
    invalid([&] { catalog.registerDefinition(malformed); });
    malformed = boxDefinition("custom.invalid");
    malformed.layout.width = std::numeric_limits<float>::quiet_NaN();
    invalid([&] { catalog.registerDefinition(malformed); });
    malformed = boxDefinition("custom.invalid");
    malformed.layout.pins[0].lead = {{-4, 0}, {-3, 0}};
    invalid([&] { catalog.registerDefinition(malformed); });
    malformed = boxDefinition("custom.invalid");
    malformed.presentation.shader.key = "unknown";
    invalid([&] { catalog.registerDefinition(malformed); });
    malformed = boxDefinition("custom.invalid");
    malformed.presentation.kind = PresentationKind::NativeSdf;
    invalid([&] { catalog.registerDefinition(malformed); });
    malformed = boxDefinition("custom.invalid");
    malformed.defaultInputState = true;
    invalid([&] { catalog.registerDefinition(malformed); });
    require(catalog.definitions().size() == count, "Invalid registration changed the catalog.");
    invalid([&] { catalog.resolve("missing"); });
    invalid([&] { catalog.resolve(definition.identity.id, {}, 2); });
    ComponentOverrides options;
    options.inputCount = 2;
    invalid([&] { catalog.resolve(BuiltinComponentIds::Not, options); });
    options.inputCount = 256;
    invalid([&] { catalog.resolve(BuiltinComponentIds::And, options); });
    options = {};
    options.inputState = true;
    invalid([&] { catalog.resolve(BuiltinComponentIds::And, options); });
    options = {};
    options.clockFrequency = std::numeric_limits<float>::infinity();
    invalid([&] { catalog.resolve(BuiltinComponentIds::Clock, options); });
    options.clockFrequency = 0.05f;
    invalid([&] { catalog.resolve(BuiltinComponentIds::Clock, options); });
    options.clockFrequency = 1.0f;
    invalid([&] { catalog.resolve(BuiltinComponentIds::Input, options); });
    auto fixed = boxDefinition("custom.fixed");
    fixed.presentation.allowResize = false;
    catalog.registerDefinition(fixed);
    options = {};
    options.layout = fixed.layout;
    options.layout->width = 0.4f;
    invalid([&] { catalog.resolve(fixed.identity.id, options); });

    auto clock = boxDefinition("custom.clock");
    clock.behavior = ClockBehavior{};
    clock.layout.pins = {{"tick", "Tick", PinType::OUTPUT, 0, {3, 0}, {}}};
    clock.defaultClockFrequency = 2.0f;
    clock.defaultClockPaused = true;
    catalog.registerDefinition(clock);
    const auto resolved = catalog.resolve(clock.identity.id);
    require(
        resolved.clockFrequency == 2.0f && resolved.clockPaused,
        "Custom source defaults were replaced by native presets."
    );

    auto expandable = *catalog.find(BuiltinComponentIds::And);
    expandable.identity = {"custom.expandable", 1};
    expandable.presentation.kind = PresentationKind::Box;
    expandable.presentation.shader = boxShaderResources();
    expandable.layout.pins[0].label = "Enable";
    catalog.registerDefinition(expandable);
    options = {};
    options.inputCount = 3;
    const auto expanded = catalog.resolve(expandable.identity.id, options);
    require(
        std::any_of(
            expanded.layout.pins.begin(),
            expanded.layout.pins.end(),
            [](const auto& pin) { return pin.id == "in.0" && pin.label == "Enable"; }
        ),
        "Arity expansion replaced a declared pin label."
    );
}

void catalogEdits()
{
    Scene scene;
    EditorActions actions(scene);
    auto definition = boxDefinition();
    auto failure = actions.apply(
        {RegisterComponentDefinition{definition},
         CreateComponent{definition.identity.id, {0, 0}, {}, PlacementPolicy::RejectOverlap, 2}}
    );
    require(
        !failure && !scene.getComponentCatalog().find(definition.identity.id) &&
            scene.getRevision() == 0 && scene.getComponentCount() == 0,
        "Invalid version partially registered/created a component."
    );
    auto creation = accepted(actions.apply(
        {RegisterComponentDefinition{definition},
         CreateComponent{definition.identity.id, {0, 0}, {}, PlacementPolicy::RejectOverlap, 3}}
    ));
    const int component = creation.createdComponentIds[0];
    const auto& view = *scene.getCommittedComponentView(component);
    require(
        scene.getTopologyBuildCount() == 1 && view.getShaderName() == "box" &&
            view.getBodyLabel() == "CUSTOM" && view.showsPinLabels() &&
            view.getInputPins()[0].id == "data" && view.getOutputPins()[0].label == "Result",
        "Registered definition did not produce coherent box metadata."
    );
    require(
        !creation.change->before->getComponentCatalog().find(definition.identity.id) &&
            creation.change->after->getComponentCatalog().find(definition.identity.id),
        "Registry publication mutated the before snapshot."
    );
    auto layout = layoutOf(view);
    layout.inputs[0].id = "renamed";
    require(
        !actions.apply({ConfigureComponent{component, layout}}),
        "Instance edit reassigned a stable pin ID."
    );
    const int source =
        scene.addComponent(BuiltinComponentIds::Input, {-10, 0}, {.inputState = true});
    accepted(actions.apply({AddWire{{{-9, 0}, {-3, 0}}}}));
    require(
        scene.propagate() == EvalOrderResult::OK &&
            scene.getLogicComponent(component)->getStateInPin(0) &&
            !scene.getLogicComponent(component)->getStateOutPin(),
        "Custom descriptor lost the registered native behavior."
    );
    const auto builds = scene.getTopologyBuildCount();
    accepted(actions.apply({RegisterComponentDefinition{boxDefinition("custom.other")}}));
    require(
        scene.getTopologyBuildCount() == builds &&
            scene.getLogicComponent(source)->getStateOutPin(),
        "Definition-only registration rebuilt simulation or changed runtime state."
    );
    const auto revision = scene.getRevision();
    failure = actions.apply(
        {CreateComponent{BuiltinComponentIds::Input, {20, 0}, {}},
         RegisterComponentDefinition{definition}}
    );
    require(
        !failure && scene.getRevision() == revision && scene.getComponentCount() == 2,
        "Duplicate definition partially committed the batch."
    );
    auto preview = actions.beginMove(component);
    require(
        preview && !actions.previewMove(*preview, {std::numeric_limits<int>::max(), 0}),
        "Preview allowed overflowing absolute pin anchors."
    );
    actions.cancelMove(*preview);
    failure = actions.apply({MoveComponent{component, {std::numeric_limits<int>::max(), 0}}});
    require(
        !failure && scene.getRevision() == revision,
        "Invalid move changed committed identity/placement."
    );
    accepted(actions.restore(*creation.change->before, scene.getRevision()));
    require(
        !scene.getComponentCatalog().find(definition.identity.id) && scene.getComponentCount() == 0,
        "Snapshot restoration lost catalog isolation."
    );
    accepted(actions.restore(*creation.change->after, scene.getRevision()));
    require(
        scene.getCommittedComponentView(component)->getDefinitionIdentity().version == 3 &&
            scene.getCommittedComponentView(component)->getInputPins()[0].id == "data",
        "Restoration lost definition/pin identities."
    );
}
} // namespace

int main(int argc, char** argv)
{
    const std::pair<const char*, void (*)()> groups[] = {
        {"component_catalog_defaults", catalogDefaults},
        {"component_definition_validation", definitionValidation},
        {"component_catalog_edits", catalogEdits}
    };
    try
    {
        bool matched = false;
        for (const auto& [name, run] : groups)
            if (argc == 1 || std::string(argv[1]) == name)
            {
                run();
                matched = true;
                std::cout << "PASS: " << name << '\n';
            }
        require(matched, "Unknown catalog test.");
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
