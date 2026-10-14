#include "EditorActions.h"

#include "Components/ComponentFactory.h"
#include "Components/Definitions/NativeDefinitions.h"
#include "Editor/InterfaceComponents.h"
#include "Editor/Scene.h"
#include "Geometry/GeometryQueries.h"
#include "Geometry/WireRouting.h"
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
            a[i].label != b[i].label || a[i].lead != b[i].lead || a[i].inverted != b[i].inverted)
            return false;
    return true;
}

GridCoords translated(GridCoords point, GridCoords offset)
{
    const auto x = std::int64_t(point.x) + offset.x;
    const auto y = std::int64_t(point.y) + offset.y;
    if (x < std::numeric_limits<int>::min() || x > std::numeric_limits<int>::max() ||
        y < std::numeric_limits<int>::min() || y > std::numeric_limits<int>::max())
        throw std::invalid_argument("Move exceeds the grid coordinate range.");
    return {static_cast<int>(x), static_cast<int>(y)};
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
    if (m_scene.isReadOnly())
        return failure(EditError::InvalidConfiguration, "Live inspection is read only.");
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
        std::vector<int> moved;
        std::vector<WireId> translatedWires;
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
        std::vector<std::pair<GridCoords, GridCoords>> interfaceMoves;
        std::vector<std::size_t> detachedMoves;
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
            if (request.label)
                getView(id).m_bodyLabel = *request.label;
            if (request.labelVisible)
                getView(id).m_showBodyLabel = *request.labelVisible;
            if (request.portId)
            {
                if (request.portId->empty() || getView(id).m_portId.empty())
                    throw std::invalid_argument("Port identity requires an input or output.");
                getView(id).m_portId = *request.portId;
            }
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
            {
                candidate.m_circuit.resizeGateInputs(id, static_cast<int>(layout.inputs.size()));
                for (const auto& pin : layout.inputs)
                    gate->setInputInverted(pin.pin_index, pin.inverted);
            }
            view.editInputPins() = layout.inputs;
            view.editOutputPins() = layout.outputs;
            view.m_size = layout.size;
            view.m_shaderName = layout.shader;
            placement[id] = PlacementPolicy::RejectSharedAnchors;
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
                                // Match authored ports by identity, not their label or current
                                // index.
                                const auto& layout = op.definition.layout;
                                for (const auto* pins :
                                     {&view->getInputPins(), &view->getOutputPins()})
                                    for (const auto& pin : *pins)
                                    {
                                        const auto from = view->getAbsolutePinGridPos(pin);
                                        const auto next = std::find_if(
                                            layout.pins.begin(),
                                            layout.pins.end(),
                                            [&](const auto& value)
                                            {
                                                return value.id == pin.id &&
                                                       value.direction == pin.type;
                                            }
                                        );
                                        const auto relative =
                                            next != layout.pins.end()
                                                ? next->anchor
                                                : translated(
                                                      pin.relative_pos,
                                                      {pin.type == PinType::INPUT ? -2 : 2, 0}
                                                  );
                                        const auto to = translated(
                                            view->getGridPosition(),
                                            rotateGridPoint(relative, view->getOrientation())
                                        );
                                        // Leave room for pin access when another port takes this
                                        // row. Removed ports get a loose end outside the box, so a
                                        // new port at their former position cannot inherit the old
                                        // wire.
                                        if (next == layout.pins.end())
                                            detachedMoves.push_back(interfaceMoves.size());
                                        interfaceMoves.emplace_back(from, to);
                                    }
                                view->editInputPins().clear();
                                view->editOutputPins().clear();
                                for (const auto& pin : layout.pins)
                                {
                                    PinUI visual{
                                        pin.direction,
                                        pin.index,
                                        PinState::DISCONNECTED,
                                        pin.anchor,
                                        pin.id,
                                        pin.label,
                                        pin.lead,
                                        pin.inverted
                                    };
                                    (pin.direction == PinType::INPUT ? view->editInputPins()
                                                                     : view->editOutputPins())
                                        .push_back(std::move(visual));
                                }
                                view->m_size = {layout.width, layout.height};
                                placement[id] = PlacementPolicy::RejectSharedAnchors;
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
                            if (op.reroute)
                                moved.push_back(op.componentId);
                            placement[op.componentId] = PlacementPolicy::RejectSharedAnchors;
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
                            {
                                configureLayout(
                                    op.componentId,
                                    ComponentFactory::resizeGateLayout(
                                        *candidate.m_catalog, view, count
                                    ),
                                    RemovedPinPolicy::RejectAttached
                                );
                                moved.push_back(op.componentId);
                            }
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
                                const auto layout = ComponentFactory::invertedGateLayout(
                                    *candidate.m_catalog, view, *op.inverted
                                );
                                gate->setInverted(*op.inverted);
                                view.m_bodyStyle.inverted = *op.inverted;
                                configureLayout(
                                    op.componentId, layout, RemovedPinPolicy::RejectAttached
                                );
                                moved.push_back(op.componentId);
                                candidate.m_circuit.markStateDirty();
                                changed = true;
                            }
                        }
                        if (op.inputInversion)
                        {
                            if (!gate)
                                throw EditFailure(
                                    EditError::InvalidConfiguration,
                                    "Input inversion requires a logic gate."
                                );
                            const auto setting = *op.inputInversion;
                            const auto layout = ComponentFactory::invertedInputLayout(
                                *candidate.m_catalog, view, setting.pinIndex, setting.inverted
                            );
                            if (gate->isInputInverted(setting.pinIndex) != setting.inverted)
                            {
                                configureLayout(
                                    op.componentId, layout, RemovedPinPolicy::RejectAttached
                                );
                                placement[op.componentId] = PlacementPolicy::RejectSharedAnchors;
                                moved.push_back(op.componentId);
                            }
                        }
                        if (op.orientation)
                        {
                            if (!supportsOrientation(definition->behavior))
                                throw std::invalid_argument(
                                    "Orientation editing requires a gate, latch, flip-flop or "
                                    "subcircuit."
                                );
                            orientationName(*op.orientation);
                            if (view.m_orientation != *op.orientation)
                            {
                                view.m_orientation = *op.orientation;
                                placement[op.componentId] = PlacementPolicy::RejectSharedAnchors;
                                moved.push_back(op.componentId);
                                changed = topologyChanged = true;
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
                        if (op.labelVisible && view.m_showBodyLabel != *op.labelVisible)
                        {
                            view.m_showBodyLabel = *op.labelVisible;
                            changed = true;
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
                    else if constexpr (std::is_same_v<T, MoveWire>)
                    {
                        auto found = candidate.m_wires.find(op.wireId);
                        if (found == candidate.m_wires.end())
                            throw EditFailure(EditError::InvalidWire, "Wire no longer exists.");
                        if (op.offset != GridCoords{})
                        {
                            auto path = found->second.getPath();
                            for (auto& point : path)
                                point = translated(point, op.offset);
                            found->second.setPath(path);
                            translatedWires.push_back(op.wireId);
                            changed = topologyChanged = true;
                        }
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
            if (policy == PlacementPolicy::RejectSharedAnchors)
            {
                if (sharesComponentAnchor(candidate.committedGeometry(), id))
                    throw EditFailure(
                        EditError::Overlap, "Components cannot share a grid center or pin position."
                    );
                continue;
            }
            auto& view = getView(id);
            int attempts = 0;
            while (policy == PlacementPolicy::FindFree
                       ? sharesComponentAnchor(candidate.committedGeometry(), id)
                       : candidate.checkOverlap(id))
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
        {
            if (!moved.empty() && !rerouteMovedWires(
                                      before->committedGeometry(),
                                      candidate.committedGeometry(),
                                      moved,
                                      candidate.m_wires,
                                      translatedWires
                                  ))
                throw EditFailure(
                    EditError::InvalidWire,
                    "No safe wire route. Hold Alt while moving to leave wires in place."
                );
            const auto geometry = candidate.committedGeometry();
            auto fittedComponents = moved;
            for (const int id : components)
                if (candidate.m_componentViews.contains(id) &&
                    placement.at(id) != PlacementPolicy::AllowOverlap &&
                    std::find(fittedComponents.begin(), fittedComponents.end(), id) ==
                        fittedComponents.end())
                    fittedComponents.push_back(id);
            if (!fittedComponents.empty() && !fitWiresToComponents(
                                                 geometry,
                                                 fittedComponents,
                                                 candidate.m_wires,
                                                 candidate.m_nextWireId,
                                                 translatedWires
                                             ))
                throw EditFailure(
                    EditError::InvalidWire,
                    "Cannot preserve nearby wire connections. Place the component in a clear area."
                );
            for (const auto index : detachedMoves)
            {
                auto& [from, to] = interfaceMoves[index];
                const bool attached = std::any_of(
                    candidate.m_wires.begin(),
                    candidate.m_wires.end(),
                    [&](const auto& entry)
                    {
                        const auto& path = entry.second.getPath();
                        return !path.empty() && (path.front() == from || path.back() == from);
                    }
                );
                if (!attached)
                    continue;
                const GridCoords direction{to.x - from.x, to.y - from.y};
                auto occupied = [&]
                {
                    for (const auto& component : geometry)
                        for (const auto& pin : component.pins)
                            if (pin.position == to)
                                return true;
                    // Stay on the former net or empty space; don't join an unrelated cable.
                    NetId formerNet = INVALID_NET_ID;
                    for (const auto& [id, wire] : candidate.m_wires)
                        if (wire.getPath().front() == from || wire.getPath().back() == from)
                            formerNet = wire.getNet();
                    for (const auto& [id, wire] : candidate.m_wires)
                        if (wire.getNet() != formerNet && wire.containsPoint(to))
                            return true;
                    return false;
                };
                int attempts = 0;
                while (occupied())
                {
                    if (++attempts > 32)
                        throw EditFailure(
                            EditError::InvalidWire,
                            "Cannot leave a disconnected wire end near the removed subcircuit pin."
                        );
                    to = translated(to, direction);
                }
            }
            if (!interfaceMoves.empty() &&
                !rerouteWireEndpoints(interfaceMoves, geometry, candidate.m_wires))
                throw EditFailure(
                    EditError::InvalidWire,
                    "Cannot reroute the updated subcircuit pins. Move nearby components or wires "
                    "and save again."
                );
            candidate.rebuildNets();
        }
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
    return beginMove({&componentId, 1}, {});
}

std::optional<MovePreviewHandle>
EditorActions::beginMove(std::span<const int> components, std::span<const WireId> wires)
{
    if (m_scene.isReadOnly())
        return std::nullopt;
    if (m_scene.m_previewToken != 0 || (components.empty() && wires.empty()))
        return std::nullopt;
    for (int id : components)
        if (!m_scene.m_componentViews.contains(id))
            return std::nullopt;
    for (WireId id : wires)
        if (!m_scene.m_wires.contains(id))
            return std::nullopt;
    std::unordered_map<int, std::unique_ptr<ComponentView>> views;
    for (const auto& [id, view] : m_scene.m_componentViews)
        views.emplace(id, view->clone());
    m_scene.m_previewViews = std::move(views);
    m_scene.m_previewComponentIds.assign(components.begin(), components.end());
    m_scene.m_previewWireIds.assign(wires.begin(), wires.end());
    for (auto* ids : {&m_scene.m_previewComponentIds, &m_scene.m_previewWireIds})
    {
        std::sort(ids->begin(), ids->end());
        ids->erase(std::unique(ids->begin(), ids->end()), ids->end());
    }
    m_scene.m_previewComponentId = components.empty() ? -1 : components.front();
    m_scene.m_previewOffset = {};
    m_scene.m_previewWires = wires.empty() ? std::map<WireId, Wire>{} : m_scene.m_wires;
    m_scene.m_previewBaseRevision = m_scene.m_revision;
    m_scene.m_previewToken = ++m_scene.m_nextPreviewToken;
    return MovePreviewHandle{m_scene.m_previewToken};
}

bool EditorActions::previewMove(MovePreviewHandle handle, GridCoords position)
{
    if (handle.token != m_scene.m_previewToken || m_scene.m_previewComponentId == -1)
        return false;
    const auto origin =
        m_scene.m_componentViews.at(m_scene.m_previewComponentId)->getGridPosition();
    const auto x = std::int64_t(position.x) - origin.x, y = std::int64_t(position.y) - origin.y;
    if (x < std::numeric_limits<int>::min() || x > std::numeric_limits<int>::max() ||
        y < std::numeric_limits<int>::min() || y > std::numeric_limits<int>::max())
        return false;
    return previewMoveBy(handle, {static_cast<int>(x), static_cast<int>(y)});
}

bool EditorActions::previewMoveBy(MovePreviewHandle handle, GridCoords offset)
{
    if (handle.token == 0 || handle.token != m_scene.m_previewToken ||
        m_scene.m_previewBaseRevision != m_scene.m_revision)
        return false;
    try
    {
        for (int id : m_scene.m_previewComponentIds)
        {
            const auto& view = *m_scene.m_componentViews.at(id);
            ComponentFactory::validatePosition(view, translated(view.getGridPosition(), offset));
        }
        auto wires = m_scene.m_previewWireIds.empty() ? std::map<WireId, Wire>{} : m_scene.m_wires;
        for (WireId id : m_scene.m_previewWireIds)
        {
            auto path = wires.at(id).getPath();
            for (auto& point : path)
                point = translated(point, offset);
            wires.at(id).setPath(path);
        }
        for (int id : m_scene.m_previewComponentIds)
            m_scene.m_previewViews.at(id)->setGridPosition(
                translated(m_scene.m_componentViews.at(id)->getGridPosition(), offset)
            );
        m_scene.m_previewWires = std::move(wires);
        m_scene.m_previewOffset = offset;
        return true;
    }
    catch (const std::invalid_argument&)
    {
        return false;
    }
}

bool EditorActions::cancelMove(MovePreviewHandle handle)
{
    if (handle.token == 0 || handle.token != m_scene.m_previewToken)
        return false;
    m_scene.m_previewViews.clear();
    m_scene.m_previewWires.clear();
    m_scene.m_previewComponentIds.clear();
    m_scene.m_previewWireIds.clear();
    m_scene.m_previewOffset = {};
    m_scene.m_previewToken = 0;
    m_scene.m_previewComponentId = -1;
    return true;
}

EditResult EditorActions::commitMove(MovePreviewHandle handle, bool reroute)
{
    if (handle.token == 0 || handle.token != m_scene.m_previewToken ||
        m_scene.m_previewBaseRevision != m_scene.m_revision)
    {
        cancelMove(handle);
        return failure(EditError::StaleRevision, "The move preview no longer matches the scene.");
    }
    EditBatch batch;
    for (int id : m_scene.m_previewComponentIds)
        batch.push_back(
            MoveComponent{id, m_scene.m_previewViews.at(id)->getGridPosition(), reroute}
        );
    for (WireId id : m_scene.m_previewWireIds)
        batch.push_back(MoveWire{id, m_scene.m_previewOffset});
    auto result = applyImpl(batch, true);
    cancelMove(handle);
    return result;
}

EditResult EditorActions::restore(const Scene& snapshot, std::uint64_t expectedRevision)
{
    if (m_scene.isReadOnly())
        return failure(EditError::InvalidConfiguration, "Live inspection is read only.");
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
