#include "Persistence/CircuitFile.h"

#include "Components/Definitions/NativeDefinitions.h"
#include "Editor/Actions/EditorActions.h"
#include "Editor/InterfaceComponents.h"
#include "Editor/Subcircuits.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <limits>
#include <nlohmann/json.hpp>
#include <set>
#include <stdexcept>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

namespace
{
using Json = nlohmann::json;
constexpr std::size_t maxFileBytes = 16 * 1024 * 1024;

int integer(const Json& value)
{
    if (!value.is_number_integer() || value < std::numeric_limits<int>::min() ||
        value > std::numeric_limits<int>::max())
        throw std::invalid_argument("Expected an integer within the grid coordinate range.");
    return value.get<int>();
}

std::uint32_t version(const Json& value)
{
    if (!value.is_number_integer() || value < 1 ||
        value > std::numeric_limits<std::uint32_t>::max())
        throw std::invalid_argument("Definition versions must be positive 32-bit integers.");
    return value.get<std::uint32_t>();
}

float number(const Json& value)
{
    if (!value.is_number() || !std::isfinite(value.get<float>()))
        throw std::invalid_argument("Expected a finite number.");
    return value.get<float>();
}

const Json& array(const Json& value)
{
    if (!value.is_array())
        throw std::invalid_argument("Expected a JSON array.");
    return value;
}

GridCoords point(const Json& value)
{
    if (array(value).size() != 2)
        throw std::invalid_argument("Grid points require exactly two coordinates.");
    return {integer(value.at(0)), integer(value.at(1))};
}

Json points(const std::vector<GridCoords>& path)
{
    Json result = Json::array();
    for (auto p : path)
        result.push_back({p.x, p.y});
    return result;
}

std::vector<GridCoords> points(const Json& value)
{
    std::vector<GridCoords> result;
    for (const auto& p : array(value))
        result.push_back(point(p));
    return result;
}

Json layoutJson(const DefinitionLayout& layout)
{
    Json pins = Json::array();
    for (const auto& pin : layout.pins)
        pins.push_back(
            {{"id", pin.id},
             {"label", pin.label},
             {"direction", pin.direction == PinType::INPUT ? "input" : "output"},
             {"index", pin.index},
             {"anchor", {pin.anchor.x, pin.anchor.y}},
             {"lead", points(pin.lead)}}
        );
    return {{"size", {layout.width, layout.height}}, {"pins", pins}};
}

DefinitionLayout readLayout(const Json& value)
{
    const auto& size = array(value.at("size"));
    if (size.size() != 2)
        throw std::invalid_argument("Component size requires width and height.");
    DefinitionLayout layout{number(size.at(0)), number(size.at(1)), {}};
    if (array(value.at("pins")).size() > 256)
        throw std::invalid_argument("Components support at most 256 pins.");
    for (const auto& pin : value.at("pins"))
    {
        const auto direction = pin.at("direction").get<std::string>();
        if (direction != "input" && direction != "output")
            throw std::invalid_argument("Unknown pin direction.");
        const int index = integer(pin.at("index"));
        if (index < 0)
            throw std::invalid_argument("Pin indices must be non-negative.");
        layout.pins.push_back(
            {pin.at("id").get<std::string>(),
             pin.at("label").get<std::string>(),
             direction == "input" ? PinType::INPUT : PinType::OUTPUT,
             static_cast<unsigned int>(index),
             point(pin.at("anchor")),
             points(pin.at("lead"))}
        );
    }
    return layout;
}

DefinitionLayout instanceLayout(const ComponentView& view)
{
    const auto size = view.getSize();
    DefinitionLayout layout{size.x, size.y, {}};
    for (const auto* pins : {&view.getInputPins(), &view.getOutputPins()})
        for (const auto& pin : *pins)
            layout.pins.push_back(
                {pin.id, pin.label, pin.type, pin.pin_index, pin.relative_pos, pin.lead}
            );
    return layout;
}

std::string behaviorId(const ComponentBehavior& behavior)
{
    if (const auto* gate = std::get_if<GateType>(&behavior))
        return builtinDefinitionId(*gate);
    if (const auto* latch = std::get_if<LatchType>(&behavior))
        return builtinDefinitionId(*latch);
    if (std::holds_alternative<ManualInputBehavior>(behavior))
        return BuiltinComponentIds::Input;
    if (std::holds_alternative<ClockBehavior>(behavior))
        return BuiltinComponentIds::Clock;
    return BuiltinComponentIds::Output;
}

// Reusable definitions carry their authored design, not runtime instance state.
Json definitionJson(const ComponentDefinition& definition)
{
    const auto& p = definition.presentation;
    Json result{
        {"id", definition.identity.id},
        {"version", definition.identity.version},
        {"name", definition.displayName},
        {"behavior",
         std::holds_alternative<SubcircuitBehavior>(definition.behavior)
             ? "subcircuit"
             : behaviorId(definition.behavior)},
        {"layout", layoutJson(definition.layout)},
        {"scalable_inputs", definition.pinLayoutRule == PinLayoutRule::SymmetricGateInputs},
        {"label", p.bodyLabel},
        {"show_pin_labels", p.showPinLabels},
        {"allow_resize", p.allowResize},
        {"tint", p.body.tint},
        {"input_state", definition.defaultInputState},
        {"clock_frequency", definition.defaultClockFrequency},
        {"clock_paused", definition.defaultClockPaused}
    };
    if (const auto* sub = std::get_if<SubcircuitBehavior>(&definition.behavior))
        result["subcircuit"] = Json::parse(
            subcircuitToJson(*sub->authored, definition.identity, definition.displayName)
        );
    return result;
}

ComponentDefinition readDefinition(const Json& value, const ComponentCatalog& natives)
{
    ComponentDefinition definition;
    definition.identity = {value.at("id").get<std::string>(), version(value.at("version"))};
    definition.displayName = value.at("name").get<std::string>();
    if (value.at("behavior") == "subcircuit")
    {
        auto saved = subcircuitFromJson(value.at("subcircuit").dump());
        if (saved.identity.id != definition.identity.id ||
            saved.identity.version != definition.identity.version ||
            saved.design.name != definition.displayName)
            throw std::invalid_argument(
                "Embedded subcircuit identity does not match its definition."
            );
        definition = makeSubcircuit(saved.design.scene, saved.identity, saved.design.name);
        const auto claimed = readLayout(value.at("layout"));
        if (layoutJson(claimed) != layoutJson(definition.layout))
            throw std::invalid_argument("Subcircuit interface does not match its authored design.");
    }
    else
    {
        const auto* behavior = natives.find(value.at("behavior").get<std::string>());
        if (!behavior || !behavior->identity.id.starts_with("native."))
            throw std::invalid_argument("Unknown native behavior in a box definition.");
        definition.behavior = behavior->behavior;
    }
    definition.layout = readLayout(value.at("layout"));
    definition.pinLayoutRule = value.at("scalable_inputs").get<bool>()
                                   ? PinLayoutRule::SymmetricGateInputs
                                   : PinLayoutRule::Fixed;
    auto& p = definition.presentation;
    p.shader = boxShaderResources();
    p.bodyLabel = value.at("label").get<std::string>();
    p.showPinLabels = value.at("show_pin_labels").get<bool>();
    p.allowResize = value.at("allow_resize").get<bool>();
    const auto& tint = array(value.at("tint"));
    if (tint.size() != 4)
        throw std::invalid_argument("Tint requires four channels.");
    for (int i = 0; i < 4; ++i)
        p.body.tint[i] = number(tint.at(i));
    definition.defaultInputState = value.at("input_state").get<bool>();
    definition.defaultClockFrequency = number(value.at("clock_frequency"));
    definition.defaultClockPaused = value.at("clock_paused").get<bool>();
    validateDefinition(definition);
    return definition;
}

void applyEdits(Scene& scene, const EditBatch& batch)
{
    const auto result = EditorActions(scene).apply(batch);
    if (!result)
        throw std::invalid_argument(result.message);
}
} // namespace

std::string circuitToJson(const Scene& scene, const std::string& name)
{
    if (scene.getComponentCount() > 4096 || scene.wireCount() > 16384)
        throw std::invalid_argument("Circuit exceeds 4096 components or 16384 wire routes.");
    Json document{
        {"format", "logic-gates-circuit"},
        {"version", 1},
        {"name", name},
        {"definitions", Json::array()},
        {"components", Json::array()},
        {"wires", Json::array()}
    };
    std::vector<int> ids;
    for (const auto& [id, view] : scene.getComponentViewMap())
        ids.push_back(id);
    std::sort(ids.begin(), ids.end());
    std::set<std::string> usedDefinitions;
    for (int id : ids)
    {
        const auto& view = *scene.getCommittedComponentView(id);
        const auto& identity = view.getDefinitionIdentity();
        const auto position = view.getGridPosition();
        const auto* definition = scene.getComponentCatalog().find(identity.id);
        if (!definition)
            throw std::invalid_argument("Cannot save a component with an unknown definition.");
        if (!identity.id.starts_with("native.") && usedDefinitions.insert(identity.id).second)
            document["definitions"].push_back(definitionJson(*definition));
        Json component{
            {"definition", identity.id},
            {"version", identity.version},
            {"position", {position.x, position.y}},
            {"label", view.getBodyLabel()},
            {"layout", layoutJson(instanceLayout(view))}
        };
        const auto* logic = scene.getLogicComponent(id);
        if (const auto* gate = dynamic_cast<const Gate*>(logic))
        {
            if (gate->getType() != NOT &&
                definition->presentation.kind == PresentationKind::NativeSdf)
                component["inverted"] = gate->isInverted();
        }
        else if (const auto* clock = dynamic_cast<const Clock*>(logic))
        {
            component["clock_frequency"] = clock->getFrequency();
            component["clock_paused"] = clock->isPaused();
        }
        else if (const auto* input = dynamic_cast<const InputPin*>(logic))
            component["input_state"] = input->getState();
        document["components"].push_back(std::move(component));
    }
    for (const auto& [id, wire] : scene.getWires())
        document["wires"].push_back(points(wire.getPath()));
    return document.dump(2) + '\n';
}

SavedCircuit circuitFromJson(std::string_view text)
{
    struct DepthGuard
    {
        unsigned& depth;

        explicit DepthGuard(unsigned& value) : depth(value)
        {
            if (depth >= 9)
                throw std::invalid_argument("Too many nested subcircuit definitions.");
            ++depth;
        }

        ~DepthGuard() { --depth; }
    };

    static thread_local unsigned nesting = 0;
    DepthGuard guard(nesting);
    if (text.size() > maxFileBytes)
        throw std::invalid_argument("Circuit files must be smaller than 16 MiB.");
    const auto document = Json::parse(
        text,
        [](int depth, Json::parse_event_t, Json&)
        {
            if (depth > 128)
                throw std::invalid_argument("Circuit JSON is nested too deeply.");
            return true;
        }
    );
    if (document.at("format") != "logic-gates-circuit" || integer(document.at("version")) != 1)
        throw std::invalid_argument("Unsupported circuit file format or version.");
    SavedCircuit saved{document.at("name").get<std::string>(), {}};
    EditBatch batch;
    for (const auto& definition : array(document.at("definitions")))
        batch.push_back(
            RegisterComponentDefinition{
                readDefinition(definition, saved.scene.getComponentCatalog())
            }
        );
    const auto& components = array(document.at("components"));
    if (components.size() > 4096 || array(document.at("wires")).size() > 16384)
        throw std::invalid_argument("Circuit exceeds 4096 components or 16384 wire routes.");
    for (const auto& component : components)
    {
        const auto componentVersion = version(component.at("version"));
        ComponentOverrides options;
        options.layout = readLayout(component.at("layout"));
        if (component.contains("inverted"))
            options.inverted = component.at("inverted").get<bool>();
        if (component.contains("input_state"))
            options.inputState = component.at("input_state").get<bool>();
        if (component.contains("clock_frequency"))
            options.clockFrequency = number(component.at("clock_frequency"));
        if (component.contains("clock_paused"))
            options.clockPaused = component.at("clock_paused").get<bool>();
        batch.push_back(
            CreateComponent{
                component.at("definition").get<std::string>(),
                point(component.at("position")),
                std::move(options),
                PlacementPolicy::AllowOverlap,
                componentVersion
            }
        );
    }
    for (const auto& path : document.at("wires"))
        batch.push_back(AddWire{points(path)});
    const auto created = EditorActions(saved.scene).apply(batch);
    if (!created)
        throw std::invalid_argument(created.message);
    batch.clear();
    for (std::size_t i = 0; i < components.size(); ++i)
    {
        ConfigureComponentProperties properties{
            .componentId = created.createdComponentIds.at(i),
            .label = components.at(i).at("label").get<std::string>()
        };
        if (components.at(i).contains("inverted"))
            properties.inverted = components.at(i).at("inverted").get<bool>();
        batch.push_back(std::move(properties));
    }
    applyEdits(saved.scene, batch);
    saved.scene.propagate();
    saved.scene.syncVisuals();
    return saved;
}

namespace
{
void writeText(const std::filesystem::path& path, const std::string& text)
{
    if (text.size() > maxFileBytes)
        throw std::invalid_argument("Circuit exceeds the save-file size limits.");
    auto temporary = path;
    temporary +=
        "." + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".tmp";
    try
    {
        std::ofstream stream;
        stream.exceptions(std::ios::failbit | std::ios::badbit);
        stream.open(temporary, std::ios::binary | std::ios::trunc);
        stream.write(text.data(), static_cast<std::streamsize>(text.size()));
        stream.close();
#ifdef _WIN32
        if (!MoveFileExW(
                temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH
            ))
            throw std::system_error(
                static_cast<int>(GetLastError()),
                std::system_category(),
                "Cannot replace circuit file"
            );
#else
        std::filesystem::rename(temporary, path);
#endif
    }
    catch (...)
    {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        throw;
    }
}

std::string readText(const std::filesystem::path& path)
{
    const auto size = std::filesystem::file_size(path);
    if (size > maxFileBytes)
        throw std::invalid_argument("Circuit files must be smaller than 16 MiB.");
    std::ifstream stream;
    stream.exceptions(std::ios::failbit | std::ios::badbit);
    stream.open(path, std::ios::binary);
    std::string text(static_cast<std::size_t>(size), '\0');
    stream.read(text.data(), static_cast<std::streamsize>(text.size()));
    if (stream.peek() != std::char_traits<char>::eof())
        throw std::invalid_argument("Circuit file changed while being read.");
    return text;
}
} // namespace

void saveCircuit(const std::filesystem::path& path, const Scene& scene, const std::string& name)
{
    if (scene.getComponentCount() > 4096 || scene.wireCount() > 16384)
        throw std::invalid_argument("Circuit exceeds the save-file size limits.");
    writeText(path, circuitToJson(scene, name));
}

SavedCircuit loadCircuit(const std::filesystem::path& path)
{
    return circuitFromJson(readText(path));
}

std::string
subcircuitToJson(const Scene& scene, DefinitionIdentity identity, const std::string& name)
{
    if (!identity.id.starts_with("subcircuit.") || identity.version == 0)
        throw std::invalid_argument("Subcircuit files require a stable subcircuit.* identity.");
    std::vector<int> ids;
    for (const auto& [id, view] : scene.getComponentViewMap())
        ids.push_back(id);
    std::sort(ids.begin(), ids.end());
    Json ports = Json::array();
    for (const auto& component : interfaceComponents(scene))
    {
        if (component.kind == InterfaceKind::Clock)
            continue;
        const auto ordinal = std::lower_bound(ids.begin(), ids.end(), component.id) - ids.begin();
        ports.push_back(
            {{"component", ordinal},
             {"name", component.name},
             {"direction", component.kind == InterfaceKind::Input ? "input" : "output"}}
        );
    }
    return Json{
               {"format", "logic-gates-subcircuit"},
               {"version", 1},
               {"id", identity.id},
               {"definition_version", identity.version},
               {"ports", ports},
               {"design", Json::parse(circuitToJson(scene, name))}
           }.dump(2) +
           '\n';
}

SavedSubcircuit subcircuitFromJson(std::string_view text)
{
    if (text.size() > maxFileBytes)
        throw std::invalid_argument("Subcircuit file exceeds 16 MiB.");
    const auto document = Json::parse(
        text,
        [](int depth, Json::parse_event_t, Json&)
        {
            if (depth > 128)
                throw std::invalid_argument("Subcircuit JSON is nested too deeply.");
            return true;
        }
    );
    if (document.at("format") != "logic-gates-subcircuit" || integer(document.at("version")) != 1)
        throw std::invalid_argument("Unsupported subcircuit file format or version.");
    SavedSubcircuit saved{
        {document.at("id").get<std::string>(), version(document.at("definition_version"))},
        circuitFromJson(document.at("design").dump())
    };
    if (!saved.identity.id.starts_with("subcircuit."))
        throw std::invalid_argument("Invalid subcircuit identity.");
    // Created IDs follow the sorted serialized component order. Check the explicit interface
    // mapping.
    const auto expected =
        Json::parse(subcircuitToJson(saved.design.scene, saved.identity, saved.design.name));
    if (document.at("ports") != expected.at("ports"))
        throw std::invalid_argument(
            "Subcircuit port order/names do not match the authored design."
        );
    saved.design.scene.setInterfaceNamingRequired(true);
    return saved;
}

void saveSubcircuit(
    const std::filesystem::path& path,
    const Scene& scene,
    DefinitionIdentity identity,
    const std::string& name
)
{
    writeText(path, subcircuitToJson(scene, std::move(identity), name));
}

SavedSubcircuit loadSubcircuit(const std::filesystem::path& path)
{
    return subcircuitFromJson(readText(path));
}
