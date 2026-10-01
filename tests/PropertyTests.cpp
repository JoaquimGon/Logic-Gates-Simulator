#include "Components/Definitions/ConfigurationCodec.h"
#include "Components/Definitions/NativeDefinitions.h"
#include "Editor/Actions/EditorActions.h"
#include "Editor/Scene.h"

#include <algorithm>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace
{
using namespace ComponentPropertyIds;

void require(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}

void accepted(const EditResult& result)
{
    if (!result)
        throw std::runtime_error(result.message);
}

template <class F>
void invalid(F run)
{
    bool rejected = false;
    try
    {
        run();
    }
    catch (const std::invalid_argument&)
    {
        rejected = true;
    }
    require(rejected, "Invalid property declaration/value/record was accepted.");
}

ComponentPropertyPatch set(const char* id, PropertyValue value)
{
    ComponentPropertyPatch patch;
    patch.values.emplace(id, std::move(value));
    return patch;
}

ComponentDefinition custom(const ComponentCatalog& catalog, const char* source, const char* id)
{
    auto result = *catalog.find(source);
    result.identity = {id, 1};
    result.presentation.kind = PresentationKind::Box;
    result.presentation.body = {};
    result.presentation.shader = boxShaderResources();
    return result;
}

void schemas()
{
    ComponentCatalog catalog;
    for (const auto& [id, definition] : catalog.definitions())
    {
        const auto descriptors = propertyDescriptors(definition);
        const auto resolved = catalog.resolve(id);
        require(
            descriptors.size() >= 4 && resolved.configuration.overrides.empty() &&
                !resolved.configuration.pinLayout,
            "Creation stored inherited values as explicit overrides."
        );
        for (const auto& descriptor : descriptors)
            require(
                resolved.properties.at(descriptor.id) == descriptor.defaultValue,
                "Schema defaults drifted from definition resolution."
            );
    }
    const auto notSchema = propertyDescriptors(*catalog.find(BuiltinComponentIds::Not));
    const auto count = std::find_if(
        notSchema.begin(), notSchema.end(), [](const auto& item) { return item.id == InputCount; }
    );
    require(
        count != notSchema.end() && !count->editable && std::get<int>(count->defaultValue) == 1,
        "NOT exposed editable multi-input metadata."
    );
    auto definition = custom(catalog, BuiltinComponentIds::And, "user.bounded");
    PropertyRule rule;
    rule.id = InputCount;
    rule.minimum = 2.0f;
    rule.maximum = 8.0f;
    rule.choices = {{2, "Two"}, {4, "Four"}, {8, "Eight"}};
    definition.propertyRules.push_back(rule);
    PropertyRule readOnly;
    readOnly.id = Width;
    readOnly.editable = false;
    definition.propertyRules.push_back(readOnly);
    catalog.registerDefinition(definition);
    ComponentOverrides options;
    options.properties[InputCount] = 4;
    auto resolved = catalog.resolve(definition.identity.id, options);
    require(
        resolved.layout.pins.size() == 5 && resolved.layout.height >= 0.4f,
        "Schema arity did not generate coherent native geometry."
    );
    for (const PropertyValue bad :
         {PropertyValue{3}, PropertyValue{9}, PropertyValue{4.0f}, PropertyValue{true}})
    {
        options.properties[InputCount] = bad;
        invalid([&] { catalog.resolve(definition.identity.id, options); });
    }
    options = {};
    options.properties[Width] = definition.layout.width;
    invalid([&] { catalog.resolve(definition.identity.id, options); });
    options.properties = {{"unknown", false}};
    invalid([&] { catalog.resolve(definition.identity.id, options); });
    options.properties = {{Height, std::numeric_limits<float>::quiet_NaN()}};
    invalid([&] { catalog.resolve(definition.identity.id, options); });
    options.properties = {{BodyLabel, std::string(1025, 'x')}};
    invalid([&] { catalog.resolve(definition.identity.id, options); });
    options = {};
    options.inputCount = 4;
    options.properties[InputCount] = 4;
    invalid([&] { catalog.resolve(definition.identity.id, options); });
    for (int kind = 0; kind < 5; ++kind)
    {
        auto bad = definition;
        if (kind == 0)
            bad.propertyRules.push_back(rule);
        if (kind == 1)
            bad.propertyRules[0].minimum = 1.0f;
        if (kind == 2)
            bad.propertyRules[0].choices = {{4, "Four"}}; // Default excluded.
        if (kind == 3)
            bad.propertyRules[0].choices = {{2.0f, "Wrong type"}};
        if (kind == 4)
            bad.propertyRules[0].id = "unimplemented.behavior";
        invalid([&] { validateDefinition(bad); });
    }
    auto boundedHeight = definition;
    PropertyRule height;
    height.id = Height;
    height.maximum = 0.4f;
    boundedHeight.propertyRules.push_back(height);
    options = {};
    options.inputCount = 8;
    invalid([&] { resolveDefinition(boundedHeight, options); });
    auto clock = custom(catalog, BuiltinComponentIds::Clock, "user.clock");
    clock.defaultClockFrequency = 2;
    clock.defaultClockPaused = true;
    PropertyRule frequency;
    frequency.id = ClockFrequency;
    frequency.minimum = 0.5f;
    frequency.maximum = 10.0f;
    frequency.choices = {{0.5f, "Slow"}, {2.0f, "Default"}, {10.0f, "Fast"}};
    clock.propertyRules.push_back(frequency);
    catalog.registerDefinition(clock);
    options = {};
    options.properties[ClockFrequency] = 1.0f;
    invalid([&] { catalog.resolve(clock.identity.id, options); });
    require(
        catalog.resolve(clock.identity.id).clockFrequency == 2,
        "Custom schema replaced declared clock defaults."
    );
}

void propertyEdits()
{
    Scene scene;
    EditorActions actions(scene);
    const int gate = scene.addComponent(BuiltinComponentIds::And, {0, 0});
    const int input = scene.addComponent(BuiltinComponentIds::Input, {-10, 0});
    const int clock = scene.addComponent(BuiltinComponentIds::Clock, {10, 0});
    auto builds = scene.getTopologyBuildCount();
    accepted(actions.apply({ConfigureProperties{input, set(InputState, true)}}));
    require(
        scene.getTopologyBuildCount() == builds &&
            scene.getLogicComponent(input)->getStateOutPin() &&
            scene.getCommittedComponentView(input)->getConfiguration().overrides.contains(
                InputState
            ),
        "Source property edit rebuilt topology or lost provenance."
    );
    scene.handleClick(input);
    require(
        !scene.getLogicComponent(input)->getStateOutPin() &&
            std::get<bool>(
                scene.getCommittedComponentView(input)->getConfiguration().overrides.at(InputState)
            ),
        "Runtime toggle overwrote configured initial state."
    );
    accepted(actions.apply({ConfigureProperties{input, set(BodyLabel, std::string{"SOURCE"})}}));
    require(
        !scene.getLogicComponent(input)->getStateOutPin(), "Unrelated edit reset a running input."
    );
    auto stateEdit = actions.apply({ConfigureInput{input, true}});
    accepted(stateEdit);
    require(
        scene.getLogicComponent(input)->getStateOutPin(),
        "Legacy action bypassed retained property edit semantics."
    );
    auto labelEdit =
        actions.apply({ConfigureProperties{gate, set(BodyLabel, std::string{"AND CUSTOM"})}});
    accepted(labelEdit);
    require(
        scene.getCommittedComponentView(gate)->getBodyLabel() == "AND CUSTOM" &&
            scene.getTopologyBuildCount() == builds &&
            !labelEdit.change->before->getCommittedComponentView(gate)
                 ->getConfiguration()
                 .overrides.contains(BodyLabel),
        "Presentation edit rebuilt topology or mutated the before snapshot."
    );
    accepted(actions.apply({ConfigureProperties{gate, set(InputCount, 4)}}));
    require(
        scene.getTopologyBuildCount() == ++builds &&
            scene.getLogicComponent(gate)->getInputPinCount() == 4 &&
            scene.getCommittedComponentView(gate)->getInputPins()[0].id == "in.0" &&
            scene.getCommittedComponentView(gate)->getBodyLabel() == "AND CUSTOM",
        "Property arity edit lost identity, unrelated overrides, or batched rebuild policy."
    );
    accepted(actions.apply({AddWire{{{-6, -3}, {-2, -3}}}}));
    builds = scene.getTopologyBuildCount();
    const auto revision = scene.getRevision();
    auto rejected = actions.apply(
        {ConfigureProperties{gate, set(BodyLabel, std::string{"SHOULD ROLL BACK"})},
         ConfigureProperties{gate, set(InputCount, 2)}}
    );
    require(
        rejected.error == EditError::AttachedPin && scene.getRevision() == revision &&
            scene.getTopologyBuildCount() == builds &&
            scene.getCommittedComponentView(gate)->getBodyLabel() == "AND CUSTOM" &&
            std::get<int>(
                scene.getCommittedComponentView(gate)->getConfiguration().overrides.at(InputCount)
            ) == 4,
        "Rejected property batch partially changed layout/provenance."
    );
    ComponentPropertyPatch resetCount;
    resetCount.reset = {InputCount};
    accepted(actions.apply({ConfigureProperties{gate, resetCount, RemovedPinPolicy::LeaveWires}}));
    require(
        scene.getLogicComponent(gate)->getInputPinCount() == 2 &&
            !scene.getCommittedComponentView(gate)->getConfiguration().overrides.contains(
                InputCount
            ),
        "Reset copied a default into override storage."
    );
    ComponentPropertyPatch resetLabel;
    resetLabel.reset = {BodyLabel};
    accepted(actions.apply({ConfigureProperties{gate, resetLabel}}));
    require(
        scene.getCommittedComponentView(gate)->getBodyLabel().empty(),
        "Reset did not restore declared label."
    );
    auto noOp = actions.apply({ConfigureProperties{gate, resetLabel}});
    require(noOp && !noOp.change, "Already inherited reset created a false edit.");
    accepted(actions.apply({ConfigureProperties{input, set(InputState, false)}}));
    require(
        scene.getCommittedComponentView(input)->getConfiguration().overrides.contains(InputState),
        "Explicit default was incorrectly treated as inheritance."
    );
    ComponentPropertyPatch conflict = set(InputState, true);
    conflict.reset = {InputState};
    require(
        !actions.apply({ConfigureProperties{input, conflict}}), "Set/reset conflict was accepted."
    );
    accepted(actions.apply({ConfigureClock{clock, 2.0f, true}}));
    scene.stepAllClocks();
    scene.togglePauseAllClocks();
    const bool state = scene.getLogicComponent(clock)->getStateOutPin();
    require(!scene.updateClocks(0.125f), "Clock phase fixture advanced prematurely.");
    accepted(actions.apply({ConfigureProperties{clock, set(PinLabels, true)}}));
    require(
        !static_cast<Clock*>(scene.getLogicComponent(clock))->isPaused() &&
            scene.getLogicComponent(clock)->getStateOutPin() == state &&
            std::get<bool>(
                scene.getCommittedComponentView(clock)->getConfiguration().overrides.at(ClockPaused)
            ),
        "Unrelated edit rewound clock runtime or initial pause configuration."
    );
    require(
        scene.updateClocks(0.125f) && scene.getLogicComponent(clock)->getStateOutPin() != state,
        "Metadata-only property edit lost accumulated clock phase."
    );
    accepted(actions.restore(*labelEdit.change->after, scene.getRevision()));
    require(
        scene.getCommittedComponentView(gate)->getBodyLabel() == "AND CUSTOM" &&
            scene.getCommittedComponentView(gate)->getConfiguration().overrides.contains(BodyLabel),
        "Restoring a snapshot lost design overrides."
    );
    auto preview = actions.beginMove(gate);
    require(
        preview && actions.apply({ConfigureProperties{gate, resetLabel}}).error ==
                       EditError::PreviewActive,
        "Property editing bypassed preview ownership."
    );
    actions.cancelMove(*preview);
}

void configurationRoundTrips()
{
    Scene scene;
    EditorActions actions(scene);
    auto customInput =
        custom(scene.getComponentCatalog(), BuiltinComponentIds::Input, "user.source");
    customInput.defaultInputState = true;
    accepted(actions.apply({RegisterComponentDefinition{customInput}}));
    const int source = scene.addComponent(customInput.identity.id, {0, 0});
    const std::string label = "quoted \"label\" and \\ path\nnext line";
    accepted(actions.apply({ConfigureProperties{source, set(BodyLabel, label)}}));
    scene.handleClick(source);
    const auto& view = *scene.getCommittedComponentView(source);
    const ComponentConfigurationRecord record{
        view.getDefinitionIdentity(), view.getConfiguration()
    };
    const auto encoded = encodeConfiguration(scene.getComponentCatalog(), record);
    const auto decoded = decodeConfiguration(scene.getComponentCatalog(), encoded);
    require(
        decoded.definition == record.definition && decoded.configuration == record.configuration &&
            encodeConfiguration(scene.getComponentCatalog(), decoded) == encoded,
        "Text escapes/provenance did not round-trip deterministically."
    );
    const int recreated = scene.addComponent(
        decoded.definition.id,
        {10, 0},
        configurationOverrides(decoded.configuration),
        PlacementPolicy::RejectOverlap,
        decoded.definition.version
    );
    require(
        scene.getLogicComponent(recreated)->getStateOutPin() &&
            !scene.getLogicComponent(source)->getStateOutPin() &&
            scene.getCommittedComponentView(recreated)->getConfiguration() == record.configuration,
        "Recreation saved runtime instead of design or changed inherited provenance."
    );
    const int clock = scene.addComponent(
        BuiltinComponentIds::Clock, {20, 0}, {.clockFrequency = 1.23456789f, .clockPaused = true}
    );
    const auto& clockView = *scene.getCommittedComponentView(clock);
    const ComponentConfigurationRecord clockRecord{
        clockView.getDefinitionIdentity(), clockView.getConfiguration()
    };
    require(
        decodeConfiguration(
            scene.getComponentCatalog(),
            encodeConfiguration(scene.getComponentCatalog(), clockRecord)
        )
                .configuration == clockRecord.configuration,
        "Float/bool overrides lost their exact values in persistence."
    );
    const int gate = scene.addComponent(BuiltinComponentIds::And, {0, 10}, {.inputCount = 3});
    auto layout = ComponentLayout{
        scene.getCommittedComponentView(gate)->getSize(),
        scene.getCommittedComponentView(gate)->getShaderName(),
        scene.getCommittedComponentView(gate)->getInputPins(),
        scene.getCommittedComponentView(gate)->getOutputPins()
    };
    layout.inputs[0].lead = {{-3, 2}, {-2, 2}};
    accepted(actions.apply({ConfigureComponent{gate, layout}}));
    const auto gateConfig = scene.getCommittedComponentView(gate)->getConfiguration();
    auto roundTrip = decodeConfiguration(
        scene.getComponentCatalog(),
        encodeConfiguration(
            scene.getComponentCatalog(),
            {scene.getCommittedComponentView(gate)->getDefinitionIdentity(), gateConfig}
        )
    );
    require(roundTrip.configuration == gateConfig, "Custom indexed pins/leads lost persistence.");
    auto rejected = actions.apply({ConfigureProperties{gate, set(InputCount, 4)}});
    require(
        !rejected && scene.getCommittedComponentView(gate)->getConfiguration() == gateConfig,
        "Arity silently discarded an explicit pin layout."
    );
    auto arity = set(InputCount, 4);
    arity.resetPinLayout = true;
    accepted(actions.apply({ConfigureProperties{gate, arity}}));
    require(
        !scene.getCommittedComponentView(gate)->getConfiguration().pinLayout &&
            scene.getLogicComponent(gate)->getInputPinCount() == 4,
        "Explicit pin regeneration did not restore generated layout."
    );
    for (auto bad :
         {std::string{"logic-component-config 2\n"},
          encoded + "garbage",
          encoded.substr(0, encoded.size() / 2),
          std::string{"logic-component-config 1\ndefinition \"native.clock\" 1\nproperties "
                      "1\n\"clock.frequencyHz\" number nan\npins 0\nend\n"},
          std::string{"logic-component-config 1\ndefinition \"native.clock\" 999\nproperties "
                      "0\npins 0\nend\n"},
          std::string{
              "logic-component-config 1\ndefinition \"native.clock\" 1\nproperties "
              "2\n\"clock.initialPaused\" bool 0\n\"clock.initialPaused\" bool 1\npins 0\nend\n"
          }})
        invalid([&] { decodeConfiguration(scene.getComponentCatalog(), bad); });
    invalid(
        [&]
        { decodeConfiguration(scene.getComponentCatalog(), std::string(8 * 1024 * 1024 + 1, 'x')); }
    );
    require(
        scene.getComponentCatalog().find(customInput.identity.id)->defaultInputState &&
            scene.getComponentCatalog()
                .find(customInput.identity.id)
                ->presentation.bodyLabel.empty(),
        "Instance editing/decoding changed reusable defaults."
    );
}
} // namespace

int main(int argc, char** argv)
{
    try
    {
        const std::string group = argc > 1 ? argv[1] : "";
        if (group == "component_property_schema")
            schemas();
        else if (group == "component_property_edits")
            propertyEdits();
        else if (group == "component_configuration_round_trips")
            configurationRoundTrips();
        else
            throw std::runtime_error("Unknown property test group.");
        std::cout << "PASS: " << group << '\n';
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
