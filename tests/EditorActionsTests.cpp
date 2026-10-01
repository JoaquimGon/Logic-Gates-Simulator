#include "Editor/Actions/EditorActions.h"
#include "Editor/Scene.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <type_traits>
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

GateLayout layout(int count = 2)
{
    GateLayout result{
        {0.2f, 0.3f}, "ANDgate", {}, {{PinType::OUTPUT, 0, PinState::DISCONNECTED, {2, 0}}}
    };
    for (int index = 0; index < count; ++index)
        result.inputs.push_back(
            {PinType::INPUT,
             static_cast<unsigned int>(index),
             PinState::DISCONNECTED,
             {-2, 2 - 2 * index}}
        );
    return result;
}

ComponentLayout currentLayout(Scene& scene, int id)
{
    const auto* view = scene.getCommittedComponentView(id);
    return {view->getSize(), view->getShaderName(), view->getInputPins(), view->getOutputPins()};
}

void atomicBatches()
{
    Circuit isolated;
    const int isolatedGate = isolated.addGate(AND, 3);
    isolated.getComponent(isolatedGate)->setStateInPin(0, true);
    isolated.getComponent(isolatedGate)->setStateInPin(1, true);
    isolated.getComponent(isolatedGate)->setStateInPin(2, true);
    isolated.getComponent(isolatedGate)->evaluate();
    isolated.resizeGateInputs(isolatedGate, 4);
    require(
        isolated.getComponent(isolatedGate)->getStateInPin(0) &&
            isolated.getComponent(isolatedGate)->getStateOutPin() &&
            !isolated.getComponent(isolatedGate)->getStateInPin(3),
        "Logical resize reset surviving state or initialized a new input high."
    );

    Scene scene;
    EditorActions actions(scene);
    auto created = accepted(actions.apply(
        {CreateInput{{0, 0}, {0.15f, 0.15f}, "inputPin", true},
         CreateClock{{10, 0}, {0.15f, 0.15f}, "clock"},
         AddWire{{{1, 0}, {5, 0}}}}
    ));
    require(scene.getTopologyBuildCount() == 1, "Batch rebuilt topology more than once.");
    require(created.createdComponentIds == std::vector<int>{0, 1}, "Creation IDs were lost.");
    require(
        created.change && created.change->before->getComponentCount() == 0,
        "Batch did not capture the complete before image."
    );

    const auto revision = scene.getRevision();
    const auto ids = scene.getWireIds();
    auto failed = actions.apply(
        {ConfigureInput{0, false}, CreateGate{AND, {20, 0}, layout()}, DeleteComponent{12345}}
    );
    require(
        failed.error == EditError::InvalidComponent && !failed.change,
        "Failed batch did not report its error."
    );
    require(
        scene.getComponentCount() == 2 && scene.getRevision() == revision &&
            scene.getTopologyBuildCount() == 1 && scene.getWireIds() == ids &&
            scene.getLogicComponent(0)->getStateOutPin(),
        "Failed batch partially applied geometry, properties, or counters."
    );
    auto third = accepted(actions.apply({CreateInput{{20, 0}, {0.15f, 0.15f}, "inputPin"}}));
    require(third.createdComponentIds[0] == 2, "Failed batch consumed component identity.");

    auto builds = scene.getTopologyBuildCount();
    accepted(actions.apply({MoveComponent{0, {10, 0}}, MoveComponent{1, {0, 0}}}));
    require(
        scene.getTopologyBuildCount() == builds + 1 &&
            scene.getCommittedComponentView(0)->getGridPosition() == GridCoords{10, 0} &&
            scene.getCommittedComponentView(1)->getGridPosition() == GridCoords{0, 0},
        "Group placement validated an intermediate overlapping state."
    );

    const auto noOpRevision = scene.getRevision();
    auto noOp = accepted(
        actions.apply({MoveComponent{0, {10, 0}}, ConfigureComponent{0, currentLayout(scene, 0)}})
    );
    require(
        !noOp.change && scene.getRevision() == noOpRevision &&
            scene.getTopologyBuildCount() == builds + 1,
        "No-op created history or rebuilt topology."
    );
    require(!accepted(actions.apply({})).change, "Empty batch created a history record.");

    auto overlap = actions.apply({CreateInput{{0, 0}, {0.15f, 0.15f}, "inputPin"}});
    require(
        overlap.error == EditError::Overlap && scene.getComponentCount() == 3,
        "Invalid creation did not roll back."
    );
    auto free = accepted(actions.apply(
        {CreateInput{{0, 0}, {0.15f, 0.15f}, "inputPin", false, PlacementPolicy::FindFree}}
    ));
    require(
        scene.getCommittedComponentView(free.createdComponentIds[0])->getGridPosition() ==
            GridCoords{3, -3},
        "Creation did not resolve placement before publication."
    );
}

void movePreviews()
{
    Scene scene;
    EditorActions actions(scene);
    auto setup = accepted(actions.apply(
        {CreateInput{{-10, 2}, {0.15f, 0.15f}, "inputPin", true},
         CreateGate{AND, {0, 0}, layout()},
         CreateClock{{10, 0}, {0.15f, 0.15f}, "clock"},
         AddWire{{{-9, 2}, {-2, 2}}}}
    ));
    const int gate = setup.createdComponentIds[1];
    scene.propagate();
    const auto net = scene.netOfPin({gate, 0}, PinType::INPUT);
    const auto wires = scene.getWireIds();
    const auto revision = scene.getRevision();
    const auto builds = scene.getTopologyBuildCount();
    auto preview = actions.beginMove(gate);
    require(
        preview.has_value() && actions.previewMove(*preview, {5, 5}),
        "Move preview could not start."
    );
    require(
        scene.getComponentView(gate)->getGridPosition() == GridCoords{5, 5} &&
            scene.getCommittedComponentView(gate)->getGridPosition() == GridCoords{0, 0},
        "Preview moved committed geometry."
    );
    scene.markSimulationDirty();
    scene.propagate();
    scene.syncVisuals();
    require(
        scene.getLogicComponent(gate)->getStateInPin(0) &&
            scene.netOfPin({gate, 0}, PinType::INPUT) == net && scene.getRevision() == revision &&
            scene.getTopologyBuildCount() == builds && scene.getWireIds() == wires,
        "Preview changed simulation connectivity or identity."
    );
    require(
        actions.apply({DeleteComponent{gate}}).error == EditError::PreviewActive,
        "An edit invalidated an active preview."
    );
    require(
        actions.cancelMove(*preview) && scene.getRevision() == revision &&
            scene.getTopologyBuildCount() == builds,
        "Cancel rebuilt or changed committed geometry."
    );

    auto next = actions.beginMove(gate);
    require(
        next.has_value() && !actions.cancelMove(*preview), "Old token cancelled a new preview."
    );
    require(actions.previewMove(*next, {10, 0}), "Preview position was not accepted.");
    require(
        actions.commitMove(*next).error == EditError::Overlap &&
            scene.getCommittedComponentView(gate)->getGridPosition() == GridCoords{0, 0} &&
            scene.getTopologyBuildCount() == builds,
        "Invalid drop partially committed."
    );
    next = actions.beginMove(gate);
    require(next.has_value() && actions.previewMove(*next, {5, 5}), "Second preview failed.");
    require(
        accepted(actions.commitMove(*next)).change != nullptr &&
            scene.getTopologyBuildCount() == builds + 1 &&
            scene.netOfPin({gate, 0}, PinType::INPUT) == INVALID_NET_ID,
        "Committed preview did not rebuild once from final geometry."
    );
    next = actions.beginMove(gate);
    require(next.has_value(), "Preview could not restart.");
    scene.rebuildNets();
    require(
        actions.commitMove(*next).error == EditError::StaleRevision, "Stale preview was committed."
    );
}

void configuration()
{
    static_assert(
        std::is_const_v<std::remove_pointer_t<decltype(std::declval<Scene&>().getWire(0))>>
    );
    static_assert(
        std::is_const_v<
            std::remove_reference_t<decltype(std::declval<ComponentView&>().getInputPins())>>
    );
    static_assert(
        std::is_const_v<
            std::remove_reference_t<decltype(std::declval<ComponentView&>().getOutputPins())>>
    );

    Scene scene;
    EditorActions actions(scene);
    auto created = accepted(actions.apply(
        {CreateGate{AND, {0, 0}, layout(3)},
         CreateInput{{-10, 2}, {0.1f, 0.1f}, "inputPin", true},
         CreateInput{{-10, 0}, {0.1f, 0.1f}, "inputPin", true},
         CreateInput{{-10, -2}, {0.1f, 0.1f}, "inputPin", true},
         CreateClock{{20, 0}, {0.15f, 0.15f}, "clock"},
         CreateLatch{LatchType::SR_LATCH, {30, 0}},
         AddWire{{{-9, 2}, {-2, 2}}},
         AddWire{{{-9, 0}, {-2, 0}}},
         AddWire{{{-9, -2}, {-2, -2}}}}
    ));
    const int gate = created.createdComponentIds[0], clock = created.createdComponentIds[4];
    const int latch = created.createdComponentIds[5];
    scene.propagate();
    require(scene.getLogicComponent(gate)->getStateOutPin(), "Initial gate did not evaluate.");
    const auto wireIds = scene.getWireIds();
    auto builds = scene.getTopologyBuildCount();
    auto rejected = actions.apply({ConfigureComponent{gate, layout(2)}});
    require(
        rejected.error == EditError::AttachedPin &&
            scene.getLogicComponent(gate)->getInputPinCount() == 3 &&
            scene.getTopologyBuildCount() == builds && scene.getWireIds() == wireIds,
        "Attached pin was silently removed."
    );
    accepted(actions.apply({ConfigureComponent{gate, layout(2), RemovedPinPolicy::LeaveWires}}));
    require(
        scene.getTopologyBuildCount() == ++builds &&
            scene.getLogicComponent(gate)->getInputPinCount() == 2 &&
            scene.getCommittedComponentView(gate)->getInputPins().size() == 2 &&
            scene.getWireIds() == wireIds,
        "Arity migration changed routes or left mismatched counts."
    );
    scene.propagate();
    require(
        scene.getLogicComponent(gate)->getStateOutPin() &&
            scene.getLogicComponent(gate)->getInConnections().size() == 2 &&
            scene.getLogicComponent(gate)->getStateInPin(0) &&
            scene.getLogicComponent(gate)->getStateInPin(1),
        "Arity migration lost surviving pins, state, or connections."
    );
    auto expanded = layout(4);
    std::reverse(expanded.inputs.begin(), expanded.inputs.end());
    accepted(actions.apply({ConfigureComponent{gate, expanded}}));
    require(
        scene.getTopologyBuildCount() == ++builds &&
            scene.getLogicComponent(gate)->getInputPinCount() == 4 &&
            scene.getCommittedComponentView(gate)->getInputPins().front().pin_index == 3 &&
            scene.getLogicComponent(gate)->getStateOutPin(),
        "Expansion lost existing state or treated pin order as identity."
    );
    scene.propagate();
    require(
        !scene.getLogicComponent(gate)->getStateOutPin() &&
            scene.getLogicComponent(gate)->getStateInPin(0) &&
            scene.getLogicComponent(gate)->getStateInPin(1),
        "Surviving indexed inputs or new disconnected input were ignored."
    );
    accepted(
        actions.apply({ConfigureComponent{gate, layout(2)}, DeleteWire{created.insertedWireIds[2]}})
    );
    require(
        scene.getTopologyBuildCount() == ++builds &&
            scene.getLogicComponent(gate)->getInputPinCount() == 2,
        "Complete batch did not allow deletion of attached geometry."
    );
    // Final interface has pin 2 again, so an intermediate shrink must not reject its wire.
    accepted(actions.apply({ConfigureComponent{gate, layout(3)}}));
    accepted(actions.apply({AddWire{{{-9, -2}, {-2, -2}}}}));
    accepted(
        actions.apply({ConfigureComponent{gate, layout(2)}, ConfigureComponent{gate, layout(3)}})
    );

    auto* latchLogic = static_cast<Latch*>(scene.getLogicComponent(latch));
    latchLogic->setStateInPin(0, true);
    latchLogic->evaluate();
    auto latchLayout = currentLayout(scene, latch);
    std::reverse(latchLayout.outputs.begin(), latchLayout.outputs.end());
    accepted(actions.apply({ConfigureComponent{latch, latchLayout}}));
    require(
        scene.getLogicComponent(latch)->getStateOutPin(0) &&
            !scene.getLogicComponent(latch)->getStateOutPin(1),
        "Layout configuration reset retained latch state."
    );
    latchLayout.outputs.pop_back();
    require(
        actions.apply({ConfigureComponent{latch, latchLayout}}).error ==
            EditError::InvalidConfiguration,
        "Native latch pin count was changed."
    );
    auto invalid = layout();
    invalid.inputs[1].pin_index = 0;
    require(
        actions.apply({ConfigureComponent{gate, invalid}}).error == EditError::InvalidConfiguration,
        "Duplicate pin identity was accepted."
    );
    invalid = layout();
    invalid.size.x = std::numeric_limits<float>::quiet_NaN();
    require(
        actions.apply({ConfigureComponent{gate, invalid}}).error == EditError::InvalidConfiguration,
        "Nonfinite layout size was accepted."
    );
    require(
        actions.apply({CreateGate{NOT, {50, 0}, layout()}}).error ==
            EditError::InvalidConfiguration,
        "NOT accepted multiple inputs."
    );
    require(
        actions.apply(
                   {ConfigureClock{clock, std::numeric_limits<float>::infinity(), false}}
        ).error == EditError::InvalidConfiguration,
        "Nonfinite clock frequency was accepted."
    );
    require(
        actions.apply({ConfigureClock{gate, 1, false}}).error == EditError::InvalidConfiguration,
        "Clock properties were applied to a gate."
    );

    builds = scene.getTopologyBuildCount();
    require(!scene.updateClocks(0.2f), "Clock phase setup unexpectedly flipped.");
    accepted(actions.apply(
        {ConfigureInput{created.createdComponentIds[1], false}, ConfigureClock{clock, 2, false}}
    ));
    require(
        scene.getTopologyBuildCount() == builds && scene.isSimulationDirty(),
        "State-only configuration unnecessarily rebuilt connectivity."
    );
    require(
        scene.updateClocks(0.06f) && scene.getLogicComponent(clock)->getStateOutPin(),
        "Clock configuration discarded the accumulated phase."
    );
    scene.propagate();
    require(
        !scene.getLogicComponent(gate)->getStateInPin(0),
        "Input property change was not propagated."
    );
}

void wireEdits()
{
    Scene scene;
    EditorActions actions(scene);
    auto added = accepted(actions.apply({AddWire{{{0, 0}, {4, 0}, {4, 4}, {8, 4}}}}));
    auto builds = scene.getTopologyBuildCount();
    auto invalid = actions.apply({DeleteWireSegment{added.insertedWireIds[0], {1, 0}, {2, 0}}});
    require(
        invalid.error == EditError::InvalidSegment && scene.wireCount() == 1 &&
            scene.getTopologyBuildCount() == builds,
        "Stale segment deleted the entire wire."
    );
    auto removed =
        accepted(actions.apply({DeleteWireSegment{added.insertedWireIds[0], {4, 4}, {4, 0}}}));
    require(
        scene.getTopologyBuildCount() == builds + 1 && scene.wireCount() == 2 &&
            scene.netCount() == 2 && removed.change->removedWireIds == added.insertedWireIds &&
            removed.change->addedWireIds.size() == 2,
        "Middle deletion did not commit two fragments once."
    );
    bool first = false, second = false;
    for (const auto& [id, wire] : scene.getWires())
    {
        first |= wire.getPath() == std::vector<GridCoords>{{0, 0}, {4, 0}};
        second |= wire.getPath() == std::vector<GridCoords>{{4, 4}, {8, 4}};
    }
    require(first && second, "Middle deletion lost surviving geometry.");
    auto ids = scene.getWireIds();
    invalid = actions.apply({DeleteWire{ids[0]}, AddWire{{{10, 0}, {11, 1}}}});
    require(
        invalid.error == EditError::InvalidWire && scene.getWireIds() == ids,
        "Invalid wire batch partially deleted an existing wire."
    );

    Scene overlapScene;
    EditorActions overlapActions(overlapScene);
    auto normalized =
        accepted(overlapActions.apply({AddWire{{{0, 0}, {6, 0}}}, AddWire{{{2, 0}, {8, 0}}}}));
    require(
        overlapScene.wireCount() == 1 && overlapScene.getTopologyBuildCount() == 1 &&
            normalized.change->addedWireIds == overlapScene.getWireIds() &&
            normalized.change->removedWireIds.empty(),
        "Change record did not describe normalized IDs."
    );
    for (WireId id : normalized.insertedWireIds)
        require(
            overlapScene.getWire(id) == nullptr, "Overlap case did not replace provisional IDs."
        );

    Scene feedback;
    EditorActions feedbackActions(feedback);
    const auto inverter = GateLayout{
        {0.2f, 0.1f},
        "NOTgate",
        {{PinType::INPUT, 0, PinState::DISCONNECTED, {-2, 0}}},
        {{PinType::OUTPUT, 0, PinState::DISCONNECTED, {1, 0}}}
    };
    accepted(feedbackActions.apply({CreateGate{NOT, {0, 0}, inverter}}));
    auto loop = accepted(feedbackActions.apply({AddWire{{{1, 0}, {1, 3}, {-2, 3}, {-2, 0}}}}));
    require(
        feedback.propagate() == EvalOrderResult::CYCLE_DETECTED &&
            !feedback.getRejectedConnections().empty() && feedback.wireCount() == 1,
        "Action silently rejected feedback instead of keeping editable paused geometry."
    );
    accepted(feedbackActions.apply({DeleteWire{loop.insertedWireIds[0]}}));
    require(feedback.propagate() == EvalOrderResult::OK, "Repair through actions did not resume.");
}

void restoreSnapshots()
{
    Scene scene;
    EditorActions actions(scene);
    auto setup = accepted(actions.apply(
        {CreateGate{AND, {0, 0}, layout(3)},
         CreateClock{{20, 0}, {0.15f, 0.15f}, "clock"},
         CreateLatch{LatchType::SR_LATCH, {30, 0}},
         AddWire{{{-8, -2}, {-2, -2}}}}
    ));
    const auto originalIds = scene.getWireIds();
    require(!scene.updateClocks(0.2f), "Clock phase setup flipped.");
    auto* latch = static_cast<Latch*>(scene.getLogicComponent(2));
    latch->setStateInPin(0, true);
    latch->evaluate();
    const auto changed = accepted(actions.apply(
        {ConfigureComponent{0, layout(2), RemovedPinPolicy::LeaveWires},
         ConfigureClock{1, 2, true},
         MoveComponent{2, {40, 0}},
         AddWire{{{-4, -2}, {2, -2}}}}
    ));
    const auto finalIds = scene.getWireIds();
    const auto builds = scene.getTopologyBuildCount();
    require(
        actions.restore(*changed.change->before, scene.getRevision() - 1).error ==
            EditError::StaleRevision,
        "Stale restore overwrote a newer edit."
    );
    accepted(actions.restore(*changed.change->before, scene.getRevision()));
    require(
        scene.getTopologyBuildCount() == builds + 1 && scene.getWireIds() == originalIds &&
            scene.getLogicComponent(0)->getInputPinCount() == 3 &&
            scene.getCommittedComponentView(2)->getGridPosition() == GridCoords{30, 0} &&
            scene.getLogicComponent(2)->getStateOutPin(0) &&
            !scene.getLogicComponent(2)->getStateOutPin(1),
        "Restore lost original geometry, IDs or state."
    );
    require(
        !scene.updateClocks(0.29f) && scene.updateClocks(0.02f),
        "Restore lost the saved clock phase."
    );
    accepted(actions.restore(*changed.change->after, scene.getRevision()));
    const auto* restoredClock = static_cast<Clock*>(scene.getLogicComponent(1));
    require(
        scene.getWireIds() == finalIds && scene.getLogicComponent(0)->getInputPinCount() == 2 &&
            scene.getCommittedComponentView(2)->getGridPosition() == GridCoords{40, 0} &&
            restoredClock->getFrequency() == 2 && restoredClock->isPaused(),
        "After image did not restore the whole normalized/configured edit."
    );

    auto late = accepted(actions.apply(
        {CreateInput{{60, 0}, {0.15f, 0.15f}, "inputPin"}, AddWire{{{60, 10}, {65, 10}}}}
    ));
    accepted(actions.restore(*setup.change->before, scene.getRevision()));
    auto replacement = accepted(actions.apply(
        {CreateInput{{70, 0}, {0.15f, 0.15f}, "inputPin"}, AddWire{{{70, 10}, {75, 10}}}}
    ));
    require(
        replacement.createdComponentIds[0] > late.createdComponentIds[0] &&
            replacement.insertedWireIds[0] > late.insertedWireIds[0],
        "Restoring old images reused previously allocated identities."
    );
}

void bodyPlacement()
{
    Scene scene;
    EditorActions actions(scene);
    accepted(actions.apply({CreateInput{{0, 0}, {0.15f, 0.15f}, "inputPin"}}));
    const auto revision = scene.getRevision();
    auto overlap = actions.apply({CreateInput{{2, 0}, {0.15f, 0.15f}, "inputPin"}});
    require(overlap.error == EditError::Overlap, "Intersecting component bodies were accepted.");
    require(
        scene.getRevision() == revision && scene.getComponentCount() == 1,
        "Rejected body overlap changed the scene."
    );
    auto boundary = accepted(actions.apply({CreateInput{{3, 0}, {0.15f, 0.15f}, "inputPin"}}));
    const int second = boundary.createdComponentIds[0];
    const auto builds = scene.getTopologyBuildCount();
    require(
        actions.apply({MoveComponent{second, {2, 0}}}).error == EditError::Overlap &&
            scene.getCommittedComponentView(second)->getGridPosition() == GridCoords{3, 0},
        "Move accepted intersecting bodies or changed committed placement."
    );
    auto enlarged = currentLayout(scene, second);
    enlarged.size.x = 0.25f;
    require(
        actions.apply({ConfigureComponent{second, enlarged}}).error == EditError::Overlap &&
            scene.getCommittedComponentView(second)->getSize().x == 0.15f &&
            scene.getTopologyBuildCount() == builds,
        "Resize partially committed an overlapping body."
    );
    auto batch =
        actions.apply({ConfigureInput{0, true}, CreateInput{{2, 0}, {0.15f, 0.15f}, "inputPin"}});
    require(
        batch.error == EditError::Overlap && !scene.getLogicComponent(0)->getStateOutPin(),
        "Overlapping batch changed runtime state before failing."
    );
    Scene automatic;
    automatic.addInputPin({0, 0}, {0.15f, 0.15f}, "inputPin");
    auto free = accepted(EditorActions(automatic).apply(
        {CreateInput{{0, 0}, {0.15f, 0.15f}, "inputPin", false, PlacementPolicy::FindFree}}
    ));
    require(
        automatic.getCommittedComponentView(free.createdComponentIds[0])->getGridPosition() ==
                GridCoords{3, -3} &&
            !automatic.checkOverlap(free.createdComponentIds[0]),
        "Automatic placement ignored the full body footprint."
    );
    automatic.addInputPin({2, 0}, {0.15f, 0.15f}, "inputPin");
    require(automatic.checkOverlap(0), "Explicit legacy overlap policy changed.");
}
} // namespace

int main(int argc, char** argv)
{
    try
    {
        const std::pair<const char*, void (*)()> tests[] = {
            {"editor_atomic_batches", atomicBatches},
            {"editor_move_previews", movePreviews},
            {"editor_configuration", configuration},
            {"editor_wire_edits", wireEdits},
            {"editor_restore_snapshots", restoreSnapshots},
            {"editor_body_placement", bodyPlacement}
        };
        bool matched = false;
        for (const auto& [name, run] : tests)
            if (argc == 1 || std::string(argv[1]) == name)
            {
                run();
                matched = true;
                std::cout << "PASS: " << name << '\n';
            }
        require(matched, "Unknown editor action test.");
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
