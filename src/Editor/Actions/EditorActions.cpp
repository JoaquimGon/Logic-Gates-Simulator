#include "EditorActions.h"

#include "Components/ComponentFactory.h"
#include "Components/Definitions/NativeDefinitions.h"
#include "Editor/InterfaceComponents.h"
#include "Editor/Scene.h"
#include "Simulation/Subcircuit.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace
{
struct EditFailure : std::runtime_error
{
    EditError reason;

    EditFailure(EditError reason, const char* message) : std::runtime_error(message), reason(reason)
    {
    }
};

EditResult failure(EditError reason, const std::string& message)
{
    EditResult result;
    result.error = reason;
    result.message = message;
    return result;
}

bool samePins(const std::vector<PinUI>& a, const std::vector<PinUI>& b)
{
    if (a.size() != b.size())
        return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (a[i].type != b[i].type || a[i].pin_index != b[i].pin_index ||
            a[i].relative_pos != b[i].relative_pos || a[i].id != b[i].id ||
            a[i].label != b[i].label || a[i].lead != b[i].lead)
            return false;
    return true;
}

Wire makeWire(const std::vector<GridCoords>& path)
{
    Wire wire;
    wire.setPath(path);
    wire.simplifyPath();
    const auto& points = wire.getPath();
    if (points.size() < 2)
        throw EditFailure(EditError::InvalidWire, "A wire needs at least one nonzero segment.");
    for (std::size_t i = 1; i < points.size(); ++i)
        if (points[i].x != points[i - 1].x && points[i].y != points[i - 1].y)
            throw EditFailure(EditError::InvalidWire, "Wire segments must follow the grid axes.");
    return wire;
}
} // namespace

EditResult EditorActions::apply(const EditBatch& batch)
{
    return applyImpl(batch, false);
}

EditResult EditorActions::publish(
    Scene candidate,
    std::shared_ptr<const Scene> before,
    std::vector<int> components,
    std::vector<WireId> wires
)
{
    auto record = std::make_shared<EditRecord>();
    record->before = std::move(before);
    record->after = std::make_shared<Scene>(candidate);
    for (const auto& [id, wire] : candidate.m_wires)
        if (!record->before->m_wires.contains(id))
            record->addedWireIds.push_back(id);
    for (const auto& [id, wire] : record->before->m_wires)
        if (!candidate.m_wires.contains(id))
            record->removedWireIds.push_back(id);
    std::erase_if(components, [&](int id) { return !candidate.m_componentViews.contains(id); });
    m_scene = std::move(candidate);
    EditResult result;
    result.createdComponentIds = std::move(components);
    result.insertedWireIds = std::move(wires);
    result.change = std::move(record);
    return result;
}

EditResult EditorActions::applyImpl(const EditBatch& batch, bool allowPreview)
{
    if (m_scene.m_previewToken != 0 && !allowPreview)
        return failure(
            EditError::PreviewActive, "Finish or cancel the active preview before editing."
        );
    if (batch.empty())
        return {};

    try
    {
        auto before = std::make_shared<Scene>(m_scene);
        Scene candidate(*before);
        std::vector<int> components;
        std::vector<WireId> wires;
        std::map<int, PlacementPolicy> placement;
        std::shared_ptr<ComponentCatalog> stagedCatalog;

        struct RemovedPin
        {
            int componentId;
            int pinIndex;
            GridCoords position;
            RemovedPinPolicy policy;
        };

        std::vector<RemovedPin> removedPins;
        bool changed = false;
        bool topologyChanged = false;

        auto getView = [&](int id) -> ComponentView&
        {
            auto found = candidate.m_componentViews.find(id);
            if (found == candidate.m_componentViews.end())
                throw EditFailure(EditError::InvalidComponent, "Component no longer exists.");
            return *found->second;
        };
        auto insert = [&](Wire wire)
        {
            wires.push_back(candidate.insertWire(std::move(wire)));
            changed = topologyChanged = true;
        };

        auto create = [&](const CreateComponent& request)
        {
            const int id = candidate.addComponentRaw(request);
            components.push_back(id);
            placement[id] = request.placement;
            changed = topologyChanged = true;
        };
        auto sourceOverrides =
            [&](const char* definitionId, glm::vec2 size, const std::string& shader)
        {
            auto resolved = candidate.m_catalog->resolve(definitionId);
            if (shader != resolved.presentation.shader.key)
                throw EditFailure(
                    EditError::InvalidConfiguration, "Source shader must match its definition."
                );
            resolved.layout.width = size.x;
            resolved.layout.height = size.y;
            ComponentOverrides options;
            options.layout = std::move(resolved.layout);
            return options;
        };

        auto configureLayout = [&](int id, const ComponentLayout& layout, RemovedPinPolicy policy)
        {
            auto& view = getView(id);
            auto* gate = dynamic_cast<Gate*>(candidate.m_circuit.getComponent(id));
            if (view.getSize() == layout.size && view.getShaderName() == layout.shader &&
                samePins(view.getInputPins(), layout.inputs) &&
                samePins(view.getOutputPins(), layout.outputs))
                return;
            for (const auto& pin : view.getInputPins())
                if (pin.pin_index >= layout.inputs.size())
                    removedPins.push_back(
                        {id,
                         static_cast<int>(pin.pin_index),
                         view.getAbsolutePinGridPos(pin),
                         policy}
                    );
            if (gate)
                candidate.m_circuit.resizeGateInputs(id, static_cast<int>(layout.inputs.size()));
            view.editInputPins() = layout.inputs;
            view.editOutputPins() = layout.outputs;
            view.m_size = layout.size;
            view.m_shaderName = layout.shader;
            placement[id] = PlacementPolicy::RejectOverlap;
            changed = topologyChanged = true;
        };

        for (const auto& operation : batch)
        {
            std::visit(
                [&](const auto& op)
                {
                    using T = std::decay_t<decltype(op)>;
                    if constexpr (std::is_same_v<T, RegisterComponentDefinition>)
                    {
                        if (!stagedCatalog)
                            stagedCatalog =
                                std::make_shared<ComponentCatalog>(*candidate.m_catalog);
                        if (op.replace)
                        {
                            const auto* old = candidate.m_catalog->find(op.definition.identity.id);
                            for (auto& [id, view] : candidate.m_componentViews)
                            {
                                if (view->getDefinitionIdentity().id != op.definition.identity.id)
                                    continue;
                                if (!old ||
                                    !std::holds_alternative<SubcircuitBehavior>(old->behavior) ||
                                    !std::holds_alternative<SubcircuitBehavior>(
                                        op.definition.behavior
                                    ))
                                    throw std::invalid_argument(
                                        "Only subcircuit placements support definition updates."
                                    );
                                const auto& a = old->layout;
                                const auto& b = op.definition.layout;
                                if (a.width != b.width || a.height != b.height ||
                                    a.pins.size() != b.pins.size())
                                    throw std::invalid_argument(
                                        "Interface changed: remove placed instances before "
                                        "publishing this draft."
                                    );
                                for (std::size_t i = 0; i < a.pins.size(); ++i)
                                    if (a.pins[i].id != b.pins[i].id ||
                                        a.pins[i].label != b.pins[i].label ||
                                        a.pins[i].direction != b.pins[i].direction ||
                                        a.pins[i].index != b.pins[i].index ||
                                        a.pins[i].anchor != b.pins[i].anchor)
                                        throw std::invalid_argument(
                                            "Interface changed: remove placed instances before "
                                            "publishing this draft."
                                        );
                                candidate.m_circuit.replaceComponent(
                                    id,
                                    std::make_unique<Subcircuit>(
                                        std::get<SubcircuitBehavior>(op.definition.behavior)
                                    )
                                );
                                view->m_definition = op.definition.identity;
                                if (view->m_bodyLabel == old->presentation.bodyLabel)
                                    view->m_bodyLabel = op.definition.presentation.bodyLabel;
                                topologyChanged = true;
                            }
                            stagedCatalog->replaceDefinition(op.definition);
                        }
                        else
                            stagedCatalog->registerDefinition(op.definition);
                        candidate.m_catalog = stagedCatalog;
                        changed = true;
                    }
                    else if constexpr (std::is_same_v<T, CreateComponent>)
                        create(op);
                    else if constexpr (std::is_same_v<T, CreateGate>)
                    {
                        const auto& definitionId = builtinDefinitionId(op.type);
                        if (op.layout.shader !=
                            candidate.m_catalog->find(definitionId)->presentation.shader.key)
                            throw EditFailure(
                                EditError::InvalidConfiguration,
                                "Gate shader must match its definition."
                            );
                        create(
                            {definitionId,
                             op.position,
                             ComponentFactory::layoutOverrides(op.layout),
                             op.placement}
                        );
                    }
                    else if constexpr (std::is_same_v<T, CreateInput>)
                    {
                        auto options =
                            sourceOverrides(BuiltinComponentIds::Input, op.size, op.shader);
                        options.inputState = op.state;
                        create({BuiltinComponentIds::Input, op.position, options, op.placement});
                    }
                    else if constexpr (std::is_same_v<T, CreateClock>)
                    {
                        auto options =
                            sourceOverrides(BuiltinComponentIds::Clock, op.size, op.shader);
                        options.clockFrequency = op.frequency;
                        create({BuiltinComponentIds::Clock, op.position, options, op.placement});
                    }
                    else if constexpr (std::is_same_v<T, CreateLatch>)
                        create({builtinDefinitionId(op.type), op.position, {}, op.placement});
                    else if constexpr (std::is_same_v<T, MoveComponent>)
                    {
                        auto& view = getView(op.componentId);
                        if (view.getGridPosition() != op.position)
                        {
                            view.setGridPosition(op.position);
                            placement[op.componentId] = PlacementPolicy::RejectOverlap;
                            changed = topologyChanged = true;
                        }
                    }
                    else if constexpr (std::is_same_v<T, DeleteComponent>)
                    {
                        getView(op.componentId);
                        candidate.m_circuit.delComponent(op.componentId);
                        candidate.m_componentViews.erase(op.componentId);
                        changed = topologyChanged = true;
                    }
                    else if constexpr (std::is_same_v<T, ConfigureComponent>)
                    {
                        auto& view = getView(op.componentId);
                        configureLayout(
                            op.componentId,
                            ComponentFactory::validateLayout(*candidate.m_catalog, view, op.layout),
                            op.removedPins
                        );
                    }
                    else if constexpr (std::is_same_v<T, ConfigureComponentProperties>)
                    {
                        auto& view = getView(op.componentId);
                        auto* component = candidate.m_circuit.getComponent(op.componentId);
                        auto* gate = dynamic_cast<Gate*>(component);
                        auto* clock = dynamic_cast<Clock*>(component);
                        const auto* definition =
                            candidate.m_catalog->find(view.getDefinitionIdentity().id);
                        if (op.label && candidate.m_requireInterfaceNames &&
                            blankComponentName(*op.label) &&
                            (dynamic_cast<InputPin*>(component) ||
                             dynamic_cast<OutputPin*>(component)))
                            throw EditFailure(
                                EditError::InvalidConfiguration,
                                "Subcircuit inputs and outputs require a name."
                            );
                        if (op.inputCount)
                        {
                            if (!gate)
                                throw EditFailure(
                                    EditError::InvalidConfiguration,
                                    "Input count editing requires a gate."
                                );
                            const int count = gate->getType() == NOT
                                                  ? *op.inputCount
                                                  : std::clamp(*op.inputCount, 2, 8);
                            if (gate->getType() == NOT && count != 1)
                                throw EditFailure(
                                    EditError::InvalidConfiguration, "NOT has exactly one input."
                                );
                            if (count != gate->getInputPinCount())
                                configureLayout(
                                    op.componentId,
                                    ComponentFactory::resizeGateLayout(
                                        *candidate.m_catalog, view, count
                                    ),
                                    RemovedPinPolicy::RejectAttached
                                );
                        }
                        if (op.inverted)
                        {
                            if (!gate || gate->getType() == NOT ||
                                definition->presentation.kind != PresentationKind::NativeSdf)
                                throw EditFailure(
                                    EditError::InvalidConfiguration,
                                    "Inversion editing requires a native paired gate."
                                );
                            if (gate->isInverted() != *op.inverted)
                            {
                                gate->setInverted(*op.inverted);
                                view.m_bodyStyle.inverted = *op.inverted;
                                view.m_shaderName =
                                    candidate.m_catalog->find(builtinDefinitionId(gate->getType()))
                                        ->presentation.shader.key;
                                candidate.m_circuit.markStateDirty();
                                changed = true;
                            }
                        }
                        if (op.clockFrequency || op.clockPaused)
                        {
                            if (!clock)
                                throw EditFailure(
                                    EditError::InvalidConfiguration,
                                    "Clock settings require a clock."
                                );
                            if (op.clockFrequency)
                            {
                                validateClockFrequency(*op.clockFrequency);
                                if (clock->getFrequency() != *op.clockFrequency)
                                {
                                    clock->setFrequency(*op.clockFrequency);
                                    changed = true;
                                }
                            }
                            if (op.clockPaused && clock->isPaused() != *op.clockPaused)
                            {
                                clock->setPaused(*op.clockPaused);
                                changed = true;
                            }
                        }
                        if (op.label && view.m_bodyLabel != *op.label)
                        {
                            view.m_bodyLabel = *op.label;
                            changed = true;
                        }
                    }
                    else if constexpr (std::is_same_v<T, ConfigureInput>)
                    {
                        getView(op.componentId);
                        auto* input = dynamic_cast<InputPin*>(
                            candidate.m_circuit.getComponent(op.componentId)
                        );
                        if (!input)
                            throw EditFailure(
                                EditError::InvalidConfiguration,
                                "Input configuration requires a manual input."
                            );
                        if (input->getState() != op.state)
                        {
                            input->setState(op.state);
                            candidate.m_circuit.markStateDirty();
                            changed = true;
                        }
                    }
                    else if constexpr (std::is_same_v<T, ConfigureClock>)
                    {
                        getView(op.componentId);
                        auto* clock =
                            dynamic_cast<Clock*>(candidate.m_circuit.getComponent(op.componentId));
                        if (!clock)
                            throw EditFailure(
                                EditError::InvalidConfiguration,
                                "Clock configuration requires a clock."
                            );
                        validateClockFrequency(op.frequency);
                        if (clock->getFrequency() != op.frequency || clock->isPaused() != op.paused)
                        {
                            clock->setFrequency(op.frequency);
                            clock->setPaused(op.paused);
                            changed = true;
                        }
                    }
                    else if constexpr (std::is_same_v<T, AddWire>)
                    {
                        insert(makeWire(op.path));
                    }
                    else if constexpr (std::is_same_v<T, DeleteWire>)
                    {
                        if (candidate.m_wires.erase(op.wireId) == 0)
                            throw EditFailure(EditError::InvalidWire, "Wire no longer exists.");
                        changed = topologyChanged = true;
                    }
                    else if constexpr (std::is_same_v<T, DeleteWireSegment>)
                    {
                        auto found = candidate.m_wires.find(op.wireId);
                        if (found == candidate.m_wires.end())
                            throw EditFailure(EditError::InvalidWire, "Wire no longer exists.");
                        const auto path = found->second.getPath();
                        std::size_t cut = path.size();
                        for (std::size_t i = 1; i < path.size(); ++i)
                            if ((path[i - 1] == op.start && path[i] == op.end) ||
                                (path[i - 1] == op.end && path[i] == op.start))
                            {
                                cut = i;
                                break;
                            }
                        if (cut == path.size())
                            throw EditFailure(
                                EditError::InvalidSegment,
                                "Selected segment no longer exists; nothing was deleted."
                            );
                        candidate.m_wires.erase(found);
                        if (cut > 1)
                            insert(
                                makeWire(std::vector<GridCoords>(path.begin(), path.begin() + cut))
                            );
                        if (cut + 1 < path.size())
                            insert(
                                makeWire(std::vector<GridCoords>(path.begin() + cut, path.end()))
                            );
                        changed = topologyChanged = true;
                    }
                },
                operation
            );
        }

        for (const auto& [id, view] : candidate.m_componentViews)
            ComponentFactory::validatePosition(*view, view->getGridPosition());
        for (const auto& [id, policy] : placement)
        {
            if (!candidate.m_componentViews.contains(id) || policy == PlacementPolicy::AllowOverlap)
                continue;
            auto& view = getView(id);
            int attempts = 0;
            while (candidate.checkOverlap(id))
            {
                if (policy != PlacementPolicy::FindFree || attempts++ >= 4096)
                    throw EditFailure(
                        EditError::Overlap, "No valid component placement; the edit was cancelled."
                    );
                auto position = view.getGridPosition();
                if (position.x == std::numeric_limits<int>::max() ||
                    position.y == std::numeric_limits<int>::min())
                    throw EditFailure(
                        EditError::Overlap, "Placement would exceed the grid coordinate range."
                    );
                view.setGridPosition({position.x + 1, position.y - 1});
                ComponentFactory::validatePosition(view, view.getGridPosition());
            }
        }
        for (const auto& removed : removedPins)
        {
            if (!candidate.m_componentViews.contains(removed.componentId))
                continue;
            const auto& view = getView(removed.componentId);
            if (removed.pinIndex <
                candidate.m_circuit.getComponent(removed.componentId)->getInputPinCount())
                continue;
            for (const auto& [id, wire] : candidate.m_wires)
            {
                if (!wire.containsPoint(removed.position))
                    continue;
                if (removed.policy == RemovedPinPolicy::RejectAttached)
                    throw EditFailure(
                        EditError::AttachedPin,
                        "Remove attached wires in this batch or explicitly leave them disconnected."
                    );
                for (const auto* pins : {&view.getInputPins(), &view.getOutputPins()})
                    for (const auto& pin : *pins)
                        if (view.getAbsolutePinGridPos(pin) == removed.position)
                            throw EditFailure(
                                EditError::AttachedPin,
                                "A retained pin cannot reuse an attached removed-pin position."
                            );
            }
        }
        if (candidate.m_requireInterfaceNames)
            for (const auto& operation : missingInterfaceNames(candidate))
            {
                const auto& name = std::get<ConfigureComponentProperties>(operation);
                getView(name.componentId).m_bodyLabel = *name.label;
                changed = true;
            }
        if (!changed)
            return {};
        if (topologyChanged)
            candidate.rebuildNets();
        else
            ++candidate.m_revision;
        return publish(
            std::move(candidate), std::move(before), std::move(components), std::move(wires)
        );
    }
    catch (const EditFailure& error)
    {
        return failure(error.reason, error.what());
    }
    catch (const std::invalid_argument& error)
    {
        return failure(EditError::InvalidConfiguration, error.what());
    }
}

std::optional<MovePreviewHandle> EditorActions::beginMove(int componentId)
{
    if (m_scene.m_previewToken != 0 || !m_scene.m_componentViews.contains(componentId))
        return std::nullopt;
    std::unordered_map<int, std::unique_ptr<ComponentView>> views;
    for (const auto& [id, view] : m_scene.m_componentViews)
        views.emplace(id, view->clone());
    m_scene.m_previewViews = std::move(views);
    m_scene.m_previewComponentId = componentId;
    m_scene.m_previewBaseRevision = m_scene.m_revision;
    m_scene.m_previewToken = ++m_scene.m_nextPreviewToken;
    return MovePreviewHandle{m_scene.m_previewToken};
}

bool EditorActions::previewMove(MovePreviewHandle handle, GridCoords position)
{
    if (handle.token == 0 || handle.token != m_scene.m_previewToken ||
        m_scene.m_previewBaseRevision != m_scene.m_revision)
        return false;
    auto& view = *m_scene.m_previewViews.at(m_scene.m_previewComponentId);
    try
    {
        ComponentFactory::validatePosition(view, position);
    }
    catch (const std::invalid_argument&)
    {
        return false;
    }
    view.setGridPosition(position);
    return true;
}

bool EditorActions::cancelMove(MovePreviewHandle handle)
{
    if (handle.token == 0 || handle.token != m_scene.m_previewToken)
        return false;
    m_scene.m_previewViews.clear();
    m_scene.m_previewToken = 0;
    m_scene.m_previewComponentId = -1;
    return true;
}

EditResult EditorActions::commitMove(MovePreviewHandle handle)
{
    if (handle.token == 0 || handle.token != m_scene.m_previewToken ||
        m_scene.m_previewBaseRevision != m_scene.m_revision)
    {
        cancelMove(handle);
        return failure(EditError::StaleRevision, "The move preview no longer matches the scene.");
    }
    const int id = m_scene.m_previewComponentId;
    const auto position = m_scene.m_previewViews.at(id)->getGridPosition();
    auto result = applyImpl({MoveComponent{id, position}}, true);
    cancelMove(handle);
    return result;
}

EditResult EditorActions::restore(const Scene& snapshot, std::uint64_t expectedRevision)
{
    if (m_scene.m_previewToken != 0)
        return failure(EditError::PreviewActive, "Finish or cancel the preview before restoring.");
    if (m_scene.m_revision != expectedRevision)
        return failure(
            EditError::StaleRevision, "The scene changed since this restore was requested."
        );
    try
    {
        auto before = std::make_shared<Scene>(m_scene);
        Scene candidate(snapshot);
        candidate.m_requireInterfaceNames = m_scene.m_requireInterfaceNames;
        if (candidate.m_requireInterfaceNames)
            for (const auto& operation : missingInterfaceNames(candidate))
            {
                const auto& name = std::get<ConfigureComponentProperties>(operation);
                candidate.m_componentViews.at(name.componentId)->m_bodyLabel = *name.label;
            }
        candidate.m_nextWireId = std::max(candidate.m_nextWireId, m_scene.m_nextWireId);
        candidate.m_nextNetId = std::max(candidate.m_nextNetId, m_scene.m_nextNetId);
        candidate.m_nextPreviewToken = m_scene.m_nextPreviewToken;
        candidate.m_circuit.preserveAllocatedIds(m_scene.m_circuit);
        candidate.m_revision = m_scene.m_revision;
        candidate.m_topologyBuildCount = m_scene.m_topologyBuildCount;
        candidate.rebuildNets();
        return publish(std::move(candidate), std::move(before));
    }
    catch (const std::invalid_argument& error)
    {
        return failure(EditError::InvalidConfiguration, error.what());
    }
}
