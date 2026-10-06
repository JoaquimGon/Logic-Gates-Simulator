#include "Editor/Subcircuits.h"

#include "Components/ComponentFactory.h"
#include "Components/Definitions/NativeDefinitions.h"
#include "Editor/InterfaceComponents.h"
#include "Geometry/GridMetrics.h"

#include <algorithm>
#include <chrono>
#include <map>
#include <random>
#include <set>
#include <stdexcept>

namespace
{
void countDesign(const Scene& scene, const std::string& id, int depth, std::size_t& count)
{
    if (depth >= 8)
        throw std::invalid_argument("Subcircuits support at most eight nested levels.");
    count += scene.getComponentCount();
    if (count > 4096)
        throw std::invalid_argument("Expanded subcircuit exceeds 4096 components.");
    for (const auto& [componentId, view] : scene.getComponentViewMap())
    {
        const auto& identity = view->getDefinitionIdentity();
        if (identity.id == id)
            throw std::invalid_argument("A subcircuit cannot contain itself.");
        const auto* definition = scene.getComponentCatalog().find(identity.id);
        if (const auto* sub = std::get_if<SubcircuitBehavior>(&definition->behavior))
            countDesign(*sub->authored, id, depth + 1, count);
    }
}
} // namespace

std::string subcircuitProblem(const Scene& scene)
{
    std::set<std::string> inputs, outputs;
    for (const auto& component : interfaceComponents(scene))
    {
        if (component.kind == InterfaceKind::Clock)
            continue;
        if (blankComponentName(component.name))
            return "Inputs and outputs need names";
        auto& names = component.kind == InterfaceKind::Input ? inputs : outputs;
        if (!names.insert(component.name).second)
            return "Input/output names must be unique per direction";
    }
    if (inputs.empty() || outputs.empty())
        return "Needs a named input and output";
    if (inputs.size() + outputs.size() > 256)
        return "Supports at most 256 external pins";
    if (scene.getLastEvalResult() != SimulationResult::OK || scene.getShortedNetCount())
        return "Fix shorted, rejected or non-converging connections before publishing";
    return {};
}

std::vector<std::string> subcircuitClockNames(const Scene& scene)
{
    std::vector<std::string> names;
    for (const auto& component : interfaceComponents(scene))
        if (component.kind == InterfaceKind::Clock)
            names.push_back(component.name);
    std::vector<int> ids;
    for (const auto& [id, view] : scene.getComponentViewMap())
        ids.push_back(id);
    std::sort(ids.begin(), ids.end());
    for (int id : ids)
    {
        const auto& view = *scene.getCommittedComponentView(id);
        const auto* definition = scene.getComponentCatalog().find(view.getDefinitionIdentity().id);
        if (const auto* sub = std::get_if<SubcircuitBehavior>(&definition->behavior))
            for (const auto& name : subcircuitClockNames(*sub->authored))
                names.push_back(definition->displayName + " #" + std::to_string(id) + "/" + name);
    }
    return names;
}

ComponentDefinition
makeSubcircuit(const Scene& scene, DefinitionIdentity identity, const std::string& name)
{
    Scene authored(scene);
    authored.propagate();
    const auto problem = subcircuitProblem(authored);
    if (!problem.empty())
        throw std::invalid_argument(problem);
    std::size_t count = 0;
    countDesign(authored, identity.id, 0, count);
    SubcircuitBehavior behavior;
    behavior.authored = std::make_shared<Scene>(authored);
    // Construct fresh runtime through the same factory as editor creation. Authoring test
    // inputs, clock phase and retained latch state must not become placement defaults.
    Circuit runtime;
    std::map<int, int> runtimeIds;
    std::vector<int> authoredIds;
    for (const auto& [id, view] : authored.getComponentViewMap())
        authoredIds.push_back(id);
    std::sort(authoredIds.begin(), authoredIds.end());
    for (int id : authoredIds)
    {
        const auto& view = *authored.getCommittedComponentView(id);
        const auto* logic = authored.getLogicComponent(id);
        auto options = ComponentFactory::layoutOverrides(
            {view.getSize(), view.getShaderName(), view.getInputPins(), view.getOutputPins()}
        );
        if (dynamic_cast<const InputPin*>(logic))
            options.inputState = false;
        if (const auto* clock = dynamic_cast<const Clock*>(logic))
        {
            options.clockFrequency = clock->getFrequency();
            options.clockPaused = clock->isPaused();
        }
        const auto created = ComponentFactory::create(
            runtime,
            authored.getComponentCatalog(),
            view.getDefinitionIdentity().id,
            {0, 0},
            options,
            view.getDefinitionIdentity().version
        );
        runtimeIds[id] = created.id;
        if (const auto* gate = dynamic_cast<const Gate*>(logic); gate && gate->getType() != NOT)
            static_cast<Gate*>(runtime.getComponent(created.id))->setInverted(gate->isInverted());
    }
    for (int id : authoredIds)
        for (const auto& edge : authored.getLogicComponent(id)->getOutConnections())
            if (!runtime.connectComponents(
                    runtimeIds.at(edge.srcComponentId),
                    edge.srcPinIndex,
                    runtimeIds.at(edge.destComponentId),
                    edge.destPinIndex
                ))
                throw std::invalid_argument("Cannot compile subcircuit connections.");
    behavior.circuit = std::make_shared<Circuit>(std::move(runtime));
    ComponentDefinition definition;
    definition.identity = std::move(identity);
    definition.displayName = name;
    for (const auto& component : interfaceComponents(authored))
    {
        if (component.kind == InterfaceKind::Clock)
            continue;
        const bool input = component.kind == InterfaceKind::Input;
        auto& ids = input ? behavior.inputs : behavior.outputs;
        const auto index = static_cast<unsigned int>(ids.size());
        ids.push_back(runtimeIds.at(component.id));
        definition.layout.pins.push_back(
            {(input ? "in." : "out.") + std::to_string(index),
             component.name,
             input ? PinType::INPUT : PinType::OUTPUT,
             index,
             {0, 0},
             {}}
        );
    }
    const int rows = static_cast<int>(std::max(behavior.inputs.size(), behavior.outputs.size()));
    definition.layout.width = 8 * GridMetrics::Spacing;
    definition.layout.height = (2 * rows + 2) * GridMetrics::Spacing;
    for (auto& pin : definition.layout.pins)
        pin.anchor = {
            pin.direction == PinType::INPUT ? -4 : 4, rows - 1 - 2 * static_cast<int>(pin.index)
        };
    definition.behavior = std::move(behavior);
    definition.presentation.shader = boxShaderResources();
    definition.presentation.bodyLabel = name;
    definition.presentation.showPinLabels = true;
    definition.presentation.allowResize = false;
    validateDefinition(definition);
    return definition;
}

std::string newSubcircuitId()
{
    static std::mt19937_64 random(std::random_device{}());
    return "subcircuit." + std::to_string(random()) + "." +
           std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
}
