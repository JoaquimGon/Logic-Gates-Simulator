#include "Components/Definitions/NativeDefinitions.h"
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
            GridCoords{1, -1},
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
            scene.netOfPin({gate, 0}, PinType::INPUT) != INVALID_NET_ID,
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
        !scene.updateClocks(0.06f) && scene.updateClocks(0.1f) &&
            scene.getLogicComponent(clock)->getStateOutPin(),
        "Clock frequency edit did not preserve fractional cycle phase."
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
        feedback.propagate() == SimulationResult::NON_CONVERGENT &&
            feedback.getRejectedConnections().empty() && feedback.wireCount() == 1,
        "Action rejected feedback instead of retaining an editable, non-convergent circuit."
    );
    accepted(feedbackActions.apply({DeleteWire{loop.insertedWireIds[0]}}));
    require(feedback.propagate() == SimulationResult::OK, "Repair through actions did not resume.");
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

void componentProperties()
{
    Scene scene;
    EditorActions actions(scene);
    const int gateId = scene.addComponent(BuiltinComponentIds::And, {0, 0});
    const int sink = scene.addComponent(BuiltinComponentIds::Not, {12, 0});
    const int clockId = scene.addComponent(BuiltinComponentIds::Clock, {24, 0});
    const int input = scene.addComponent(BuiltinComponentIds::Input, {36, 0});
    accepted(actions.apply({AddWire{{{2, 0}, {10, 0}}}}));
    scene.propagate();
    require(
        !scene.getLogicComponent(gateId)->getStateOutPin() &&
            scene.getLogicComponent(sink)->getStateOutPin(),
        "Inversion fixture is not connected."
    );
    const auto wireIds = scene.getWireIds();
    const auto builds = scene.getTopologyBuildCount();
    const auto beforeLayout = currentLayout(scene, gateId);
    const auto edit = accepted(actions.apply(
        {ConfigureComponentProperties{.componentId = gateId, .label = "My gate", .inverted = true}}
    ));
    scene.propagate();
    auto* gate = static_cast<Gate*>(scene.getLogicComponent(gateId));
    const auto* view = scene.getCommittedComponentView(gateId);
    require(
        gate->getType() == NAND && gate->isInverted() && view->getBodyStyle().inverted &&
            view->getShaderName() == "NANDgate" && view->getBodyLabel() == "My gate" &&
            view->getSize() == glm::vec2(beforeLayout.size.x * 1.5f, beforeLayout.size.y) &&
            view->getOutputPins()[0].relative_pos ==
                GridCoords{
                    beforeLayout.outputs[0].relative_pos.x + 1,
                    beforeLayout.outputs[0].relative_pos.y
                } &&
            scene.getWireIds() == wireIds && scene.getTopologyBuildCount() == builds + 1 &&
            gate->getStateOutPin() && !scene.getLogicComponent(sink)->getStateOutPin(),
        "Inversion failed to preserve body proportions or reroute its output connection."
    );
    require(
        scene.getComponentCatalog()
                .find(BuiltinComponentIds::And)
                ->presentation.bodyLabel.empty() &&
            !scene.getComponentCatalog().find(BuiltinComponentIds::And)->presentation.body.inverted,
        "Instance edit mutated reusable defaults."
    );
    auto noOp = actions.apply(
        {ConfigureComponentProperties{.componentId = gateId, .label = "My gate", .inverted = true}}
    );
    require(noOp && !noOp.change, "Unchanged fields produced a false edit.");
    accepted(actions.restore(*edit.change->before, scene.getRevision()));
    require(
        static_cast<Gate*>(scene.getLogicComponent(gateId))->getType() == AND &&
            scene.getCommittedComponentView(gateId)->getBodyLabel().empty(),
        "Undo lost gate behavior or label."
    );
    accepted(actions.restore(*edit.change->after, scene.getRevision()));
    require(
        static_cast<Gate*>(scene.getLogicComponent(gateId))->isInverted() &&
            scene.getCommittedComponentView(gateId)->getShaderName() == "NANDgate" &&
            scene.getCommittedComponentView(gateId)->getBodyLabel() == "My gate",
        "Redo lost gate behavior or appearance."
    );
    // Layout edits must also work after switching to the paired native shader.
    accepted(actions.apply({ConfigureComponent{gateId, currentLayout(scene, gateId)}}));
    accepted(
        actions.apply({ConfigureComponentProperties{.componentId = gateId, .inputCount = 100}})
    );
    require(
        scene.getLogicComponent(gateId)->getInputPinCount() == 8 &&
            scene.getCommittedComponentView(gateId)->getInputPins().size() == 8 &&
            scene.getCommittedComponentView(gateId)->getInputPins()[0].id == "in.0" &&
            scene.getCommittedComponentView(gateId)->getShaderName() == "NANDgate" &&
            scene.getCommittedComponentView(gateId)->getBodyLabel() == "My gate",
        "Scalable count was not clamped or lost unrelated settings."
    );
    const auto* eight = scene.getCommittedComponentView(gateId);
    const auto removedAnchor = eight->getAbsolutePinGridPos(eight->getInputPins().back());
    accepted(actions.apply({AddWire{{{removedAnchor.x - 4, removedAnchor.y}, removedAnchor}}}));
    const auto revision = scene.getRevision();
    auto rejected = actions.apply(
        {ConfigureComponentProperties{.componentId = clockId, .clockFrequency = 2.0f},
         ConfigureComponentProperties{
             .componentId = gateId, .label = "Rejected", .inputCount = -20
         }}
    );
    require(
        rejected.error == EditError::AttachedPin && scene.getRevision() == revision &&
            scene.getLogicComponent(gateId)->getInputPinCount() == 8 &&
            scene.getCommittedComponentView(gateId)->getBodyLabel() == "My gate" &&
            static_cast<Clock*>(scene.getLogicComponent(clockId))->getFrequency() == 1,
        "Wired shrink partially committed a batch."
    );
    const auto attached = scene.getWireIds();
    EditBatch shrink;
    for (auto id : attached)
        if (scene.getWire(id)->containsPoint(removedAnchor))
            shrink.push_back(DeleteWire{id});
    shrink.push_back(
        ConfigureComponentProperties{.componentId = gateId, .label = "", .inputCount = -20}
    );
    accepted(actions.apply(shrink));
    require(
        scene.getLogicComponent(gateId)->getInputPinCount() == 2 &&
            scene.getCommittedComponentView(gateId)->getBodyLabel().empty() &&
            scene.getCommittedComponentView(gateId)->getOutputPins()[0].relative_pos ==
                GridCoords{
                    beforeLayout.outputs[0].relative_pos.x + 1,
                    beforeLayout.outputs[0].relative_pos.y
                },
        "Explicit wire removal did not allow shrink or clear the label."
    );

    for (const auto& bad :
         {ConfigureComponentProperties{.componentId = input, .inputCount = 3},
          ConfigureComponentProperties{.componentId = input, .inverted = true},
          ConfigureComponentProperties{.componentId = gateId, .clockPaused = true},
          ConfigureComponentProperties{.componentId = sink, .inputCount = 2},
          ConfigureComponentProperties{.componentId = sink, .inverted = false},
          ConfigureComponentProperties{.componentId = clockId, .clockFrequency = 0.01f},
          ConfigureComponentProperties{
              .componentId = clockId, .clockFrequency = std::numeric_limits<float>::quiet_NaN()
          },
          ConfigureComponentProperties{
              .componentId = clockId, .clockFrequency = std::numeric_limits<float>::infinity()
          }})
        require(
            actions.apply({bad}).error == EditError::InvalidConfiguration,
            "Unsupported component setting was accepted."
        );
    require(
        actions.apply({ConfigureComponentProperties{999}}).error == EditError::InvalidComponent,
        "Missing component was accepted."
    );
    const auto fixed =
        actions.apply({ConfigureComponentProperties{.componentId = sink, .inputCount = 1}});
    require(fixed && !fixed.change, "NOT one-input no-op changed the scene.");

    accepted(actions.apply(
        {ConfigureComponentProperties{.componentId = clockId, .clockFrequency = 2.0f}}
    ));
    require(!scene.updateClocks(0.125f), "Clock phase fixture advanced too soon.");
    const auto clockBuilds = scene.getTopologyBuildCount();
    accepted(actions.apply({ConfigureComponentProperties{
        .componentId = clockId, .label = "Clock", .clockPaused = true
    }}));
    require(
        static_cast<Clock*>(scene.getLogicComponent(clockId))->getFrequency() == 2 &&
            !scene.updateClocks(0.125f),
        "Pause reset frequency or advanced time."
    );
    accepted(
        actions.apply({ConfigureComponentProperties{.componentId = clockId, .clockPaused = false}})
    );
    require(
        scene.updateClocks(0.125f) && scene.getTopologyBuildCount() == clockBuilds,
        "Partial clock edits lost phase or rebuilt topology."
    );
    scene.handleClick(input);
    accepted(
        actions.apply({ConfigureComponentProperties{.componentId = input, .label = "Source"}})
    );
    require(scene.getLogicComponent(input)->getStateOutPin(), "Label edit rewound a live input.");
    auto preview = actions.beginMove(gateId);
    require(
        preview &&
            actions.apply({ConfigureComponentProperties{.componentId = gateId, .label = "Preview"}})
                    .error == EditError::PreviewActive,
        "Property edit bypassed preview ownership."
    );
    actions.cancelMove(*preview);


    Scene boxes;
    EditorActions boxActions(boxes);
    auto box = *boxes.getComponentCatalog().find(BuiltinComponentIds::And);
    box.identity = {"educational.and", 1};
    box.presentation.kind = PresentationKind::Box;
    box.presentation.shader = boxShaderResources();
    box.presentation.body = {};
    accepted(boxActions.apply(
        {RegisterComponentDefinition{box}, CreateComponent{box.identity.id, {0, 0}}}
    ));
    auto boxLayout = currentLayout(boxes, 0);
    boxLayout.size.x = 0.4f;
    boxLayout.inputs[0].lead = {{-3, 1}, {-2, 1}};
    accepted(boxActions.apply({ConfigureComponent{0, boxLayout}}));
    accepted(boxActions.apply({ConfigureComponentProperties{.componentId = 0, .inputCount = 4}}));
    const auto* boxView = boxes.getCommittedComponentView(0);
    require(
        boxView->getSize().x == 0.4f && boxView->getInputPins()[0].id == "in.0" &&
            boxView->getInputPins()[0].lead == std::vector<GridCoords>{{-3, 3}, {-2, 3}} &&
            boxView->getOutputPins()[0].relative_pos == boxLayout.outputs[0].relative_pos &&
            boxes.getComponentCatalog().find(box.identity.id)->layout.pins.size() == 3,
        "Custom arity edit lost current geometry/leads or mutated defaults."
    );
    require(
        boxActions.apply(
                      {ConfigureComponentProperties{.componentId = 0, .inverted = true}}
        ).error == EditError::InvalidConfiguration,
        "Box accepted an unsupported inversion bubble."
    );

    // Native pair toggles use the actual gate behavior for every truth-table row.
    for (const auto type : {AND, NAND, OR, NOR, XOR, NXOR})
    {
        Scene paired;
        const int id = paired.addComponent(builtinDefinitionId(type), {0, 0}, {.inputCount = 3});
        for (bool inverted : {false, true, false})
        {
            accepted(EditorActions(paired).apply(
                {ConfigureComponentProperties{.componentId = id, .inverted = inverted}}
            ));
            auto* edited = static_cast<Gate*>(paired.getLogicComponent(id));
            const GateType base = type == AND || type == NAND ? AND
                                  : type == OR || type == NOR ? OR
                                                              : XOR;
            for (int bits = 0; bits < 8; ++bits)
            {
                Gate reference(-1, base, 3);
                auto copy = edited->clone();
                for (int i = 0; i < 3; ++i)
                {
                    reference.setStateInPin(i, (bits & (1 << i)) != 0);
                    copy->setStateInPin(i, (bits & (1 << i)) != 0);
                }
                reference.evaluate();
                copy->evaluate();
                require(
                    copy->getStateOutPin() == (reference.getStateOutPin() != inverted),
                    "Edited gate truth table disagrees with inversion."
                );
            }
        }
    }
}

void inputInversion()
{
    // Compare every raw truth-table row with explicitly complemented reference inputs.
    for (auto type : {NOT, AND, NAND, OR, NOR, XOR, NXOR})
        for (int count : (type == NOT ? std::vector<int>{1} : std::vector<int>{2, 8}))
            for (int inversionMask : {0, 1, (1 << count) - 1, 0x55 & ((1 << count) - 1)})
            {
                Gate gate(0, type, count), reference(1, type, count);
                for (int pin = 0; pin < count; ++pin)
                    gate.setInputInverted(pin, (inversionMask & (1 << pin)) != 0);
                for (int bits = 0; bits < (1 << count); ++bits)
                {
                    for (int pin = 0; pin < count; ++pin)
                    {
                        const bool raw = (bits & (1 << pin)) != 0;
                        gate.setStateInPin(pin, raw);
                        reference.setStateInPin(pin, raw != gate.isInputInverted(pin));
                    }
                    gate.evaluate();
                    reference.evaluate();
                    require(
                        gate.getStateOutPin() == reference.getStateOutPin(),
                        "Input inversion changed the wrong truth-table row."
                    );
                    require(
                        gate.getStateInPin(0) == ((bits & 1) != 0),
                        "Gate evaluation overwrote the raw incoming signal."
                    );
                }
            }

    Scene scene;
    const int source =
        scene.addComponent(BuiltinComponentIds::Input, {-12, 1}, {.inputState = true});
    const int id = scene.addComponent(BuiltinComponentIds::Or, {0, 0});
    EditorActions actions(scene);
    accepted(actions.apply({AddWire{{{-11, 1}, {-2, 1}}}}));
    scene.propagate();
    const auto size = scene.getCommittedComponentView(id)->getSize();
    const auto bounds = scene.getCommittedComponentView(id)->getBodyBounds();
    const auto builds = scene.getTopologyBuildCount();
    auto set = [&](unsigned int pin, bool inverted)
    {
        auto result = accepted(actions.apply({ConfigureComponentProperties{
            .componentId = id, .inputInversion = ConfigureGateInput{pin, inverted}
        }}));
        scene.propagate();
        scene.syncVisuals();
        return result;
    };
    const auto first = set(0, true);
    auto gate = [&] { return static_cast<const Gate*>(scene.getLogicComponent(id)); };
    auto view = [&] { return scene.getCommittedComponentView(id); };
    require(
        first.change && gate()->isInputInverted(0) && gate()->getStateInPin(0) &&
            !gate()->getStateOutPin() && view()->getInputPins()[0].inverted &&
            view()->getInputPins()[0].relative_pos == GridCoords{-3, 1} &&
            view()->getInputPins()[1].relative_pos == GridCoords{-3, -1} &&
            view()->getSize() == size && view()->getBodyBounds().left < bounds.left &&
            scene.getTopologyBuildCount() == builds + 1,
        "Input edit did not preserve the wire, expand one cell, or invert its signal."
    );
    require(
        gate()->getInConnections().size() == 1 &&
            gate()->getInConnections()[0].srcComponentId == source &&
            scene.pinState({id, 0}, PinType::INPUT) == PinState::ON,
        "Input inversion altered physical wire/pin state or disconnected its driver."
    );
    accepted(actions.restore(*first.change->before, scene.getRevision()));
    scene.propagate();
    require(
        !gate()->isInputInverted(0) && gate()->getStateOutPin() &&
            view()->getInputPins()[0].relative_pos.x == -2,
        "Undo did not restore geometry, wiring and input inversion together."
    );
    accepted(actions.restore(*first.change->after, scene.getRevision()));
    scene.propagate();
    require(gate()->isInputInverted(0) && !gate()->getStateOutPin(), "Redo lost input inversion.");
    const auto revision = scene.getRevision();
    require(
        !set(0, true).change && scene.getRevision() == revision,
        "Repeated input setting created a spurious edit."
    );
    set(1, true);
    require(
        view()->getInputPins()[0].relative_pos.x == -3 && gate()->getStateOutPin(),
        "Second inversion expanded the input side again."
    );
    accepted(actions.apply({ConfigureComponentProperties{.componentId = id, .inverted = true}}));
    scene.propagate();
    require(
        gate()->isInputInverted(0) && gate()->isInputInverted(1) && !gate()->getStateOutPin(),
        "Output inversion lost input settings."
    );
    accepted(actions.apply({ConfigureComponentProperties{.componentId = id, .inputCount = 8}}));
    require(
        gate()->isInputInverted(0) && gate()->isInputInverted(1) && !gate()->isInputInverted(7),
        "Resize lost surviving inversion or inverted new pins."
    );
    set(7, true);
    set(0, false);
    set(1, false);
    require(
        view()->getInputPins()[0].relative_pos.x == -3,
        "Clearing one input removed room needed by another bubble."
    );
    accepted(actions.apply({ConfigureComponentProperties{.componentId = id, .inputCount = 2}}));
    require(
        !gate()->isInputInverted(0) && view()->getInputPins()[0].relative_pos.x == -2,
        "Removing the last inverted input left the extra input cell behind."
    );
    set(0, true);
    set(0, false);
    require(
        view()->getInputPins()[0].relative_pos.x == -2 && gate()->getInConnections().size() == 1,
        "Removing the final bubble did not restore terminals and their wire."
    );
    const auto stable = scene.getRevision();
    for (const auto operation :
         {ConfigureComponentProperties{
              .componentId = id, .inputInversion = ConfigureGateInput{9, true}
          },
          ConfigureComponentProperties{
              .componentId = source, .inputInversion = ConfigureGateInput{0, true}
          }})
        require(
            actions.apply({operation}).error == EditError::InvalidConfiguration &&
                scene.getRevision() == stable,
            "Invalid input inversion partially committed."
        );
    // Creation/import reads inversion directly from the explicit pin layout.
    auto layout = currentLayout(scene, id);
    layout.outputs[0].inverted = true;
    require(
        actions.apply({ConfigureComponent{id, layout}}).error == EditError::InvalidConfiguration,
        "Input inversion metadata was accepted on an output pin."
    );
}

void gateOrientation()
{
    Scene scene;
    const int a = scene.addComponent(BuiltinComponentIds::Input, {-12, 1}, {.inputState = true});
    const int b = scene.addComponent(BuiltinComponentIds::Input, {-12, -5});
    const int gate = scene.addComponent(BuiltinComponentIds::And, {0, 0});
    const int sink = scene.addComponent(BuiltinComponentIds::Output, {12, 0});
    EditorActions actions(scene);
    accepted(actions.apply(
        {AddWire{{{-11, 1}, {-2, 1}}},
         AddWire{{{-11, -5}, {-6, -5}, {-6, -1}, {-2, -1}}},
         AddWire{{{2, 0}, {11, 0}}}}
    ));
    const auto original = currentLayout(scene, gate);
    for (const auto direction :
         {Orientation::Up, Orientation::Down, Orientation::Left, Orientation::Right})
    {
        const auto previous = scene.getCommittedComponentView(gate)->getOrientation();
        const auto builds = scene.getTopologyBuildCount();
        const auto result = accepted(actions.apply(
            {ConfigureComponentProperties{.componentId = gate, .orientation = direction}}
        ));
        scene.propagate();
        const auto* view = scene.getCommittedComponentView(gate);
        require(
            view->getOrientation() == direction && view->getSize() == original.size &&
                view->getAbsolutePinGridPos(view->getInputPins()[0]) ==
                    rotateGridPoint(GridCoords{-2, 1}, direction) &&
                view->getAbsolutePinGridPos(view->getOutputPins()[0]) ==
                    rotateGridPoint(GridCoords{2, 0}, direction) &&
                scene.getTopologyBuildCount() == builds + 1,
            "Gate orientation lost its local layout or rebuilt topology more than once."
        );
        require(
            scene.netOfPin({a, 0}, PinType::OUTPUT) == scene.netOfPin({gate, 0}, PinType::INPUT) &&
                scene.netOfPin({b, 0}, PinType::OUTPUT) ==
                    scene.netOfPin({gate, 1}, PinType::INPUT) &&
                scene.netOfPin({gate, 0}, PinType::OUTPUT) ==
                    scene.netOfPin({sink, 0}, PinType::INPUT) &&
                scene.getLogicComponent(gate)->getStateInPin(0) &&
                !scene.getLogicComponent(gate)->getStateInPin(1),
            "Rotation swapped input identities or detached an attached wire."
        );
        accepted(actions.restore(*result.change->before, scene.getRevision()));
        require(
            scene.getCommittedComponentView(gate)->getOrientation() == previous,
            "Undo lost gate orientation."
        );
        accepted(actions.restore(*result.change->after, scene.getRevision()));
        require(
            scene.getCommittedComponentView(gate)->getOrientation() == direction,
            "Redo lost gate orientation."
        );
    }
    const auto revision = scene.getRevision();
    require(
        !accepted(
             actions.apply({ConfigureComponentProperties{
                 .componentId = gate, .orientation = Orientation::Right
             }})
        ).change &&
            scene.getRevision() == revision,
        "Selecting the current orientation added an edit."
    );
    require(
        actions.apply(
                   {ConfigureComponentProperties{.componentId = a, .orientation = Orientation::Up}}
        ).error == EditError::InvalidConfiguration &&
            actions.apply(
                       {ConfigureComponentProperties{
                           .componentId = gate, .orientation = static_cast<Orientation>(99)
                       }}
            ).error == EditError::InvalidConfiguration &&
            scene.getRevision() == revision,
        "Invalid orientation partially committed."
    );
    Scene edited;
    const int id = edited.addComponent(BuiltinComponentIds::Xor, {0, 0});
    accepted(EditorActions(edited).apply({ConfigureComponentProperties{
        .componentId = id,
        .inputCount = 8,
        .inverted = true,
        .inputInversion = ConfigureGateInput{7, true},
        .orientation = Orientation::Up
    }}));
    const auto* view = edited.getCommittedComponentView(id);
    require(
        view->getAbsolutePinGridPos(view->getInputPins()[7]) == GridCoords{4, -3} &&
            view->getAbsolutePinGridPos(view->getOutputPins()[0]) == GridCoords{0, 3},
        "Rotated arity/inversion edits moved the wrong pins."
    );
    const auto bounds = view->getBodyBounds();
    require(
        edited.hitTest(view->getPosition(), view->getGridPosition()).componentId == id &&
            bounds.width() > bounds.height(),
        "Rotated bounds/hit testing stayed unrotated."
    );
    accepted(EditorActions(edited).apply(
        {ConfigureComponentProperties{.componentId = id, .inputCount = 2, .inverted = false}}
    ));
    view = edited.getCommittedComponentView(id);
    require(
        view->getOrientation() == Orientation::Up &&
            view->getAbsolutePinGridPos(view->getInputPins()[0]) == GridCoords{-1, -2} &&
            view->getAbsolutePinGridPos(view->getOutputPins()[0]) == GridCoords{0, 2},
        "Changing pin count or output inversion reset the orientation."
    );
    Scene overflow;
    const auto refused = EditorActions(overflow).apply({CreateComponent{
        .definitionId = BuiltinComponentIds::Not,
        .position = {0, std::numeric_limits<int>::max()},
        .orientation = Orientation::Up
    }});
    require(
        !refused && overflow.getComponentCount() == 0,
        "Rotated pin coordinate overflow was accepted."
    );
}

void latchOrientation()
{
    for (const auto type : {LatchType::SR_LATCH, LatchType::D_LATCH})
    {
        Scene scene;
        const int a =
            scene.addComponent(BuiltinComponentIds::Input, {-16, 1}, {.inputState = true});
        const int b = scene.addComponent(
            BuiltinComponentIds::Input, {-16, -1}, {.inputState = type == LatchType::D_LATCH}
        );
        const int latch = scene.addComponent(builtinDefinitionId(type), {0, 0});
        const int q = scene.addComponent(BuiltinComponentIds::Output, {16, 1});
        const int nq = scene.addComponent(BuiltinComponentIds::Output, {16, -1});
        EditorActions actions(scene);
        accepted(actions.apply(
            {AddWire{{{-15, 1}, {-3, 1}}},
             AddWire{{{-15, -1}, {-3, -1}}},
             AddWire{{{3, 1}, {15, 1}}},
             AddWire{{{3, -1}, {15, -1}}}}
        ));
        scene.propagate();
        for (const auto direction :
             {Orientation::Up, Orientation::Down, Orientation::Left, Orientation::Right})
        {
            const auto previous = scene.getCommittedComponentView(latch)->getOrientation();
            const auto result = accepted(actions.apply(
                {ConfigureComponentProperties{.componentId = latch, .orientation = direction}}
            ));
            scene.propagate();
            const auto* view = scene.getCommittedComponentView(latch);
            for (const auto* pins : {&view->getInputPins(), &view->getOutputPins()})
                for (const auto& pin : *pins)
                    require(
                        view->getAbsolutePinGridPos(pin) ==
                            rotateGridPoint(pin.relative_pos, direction),
                        "Latch rotation changed a pin identity or left its anchor unrotated."
                    );
            require(
                scene.netOfPin({a, 0}, PinType::OUTPUT) ==
                        scene.netOfPin({latch, 0}, PinType::INPUT) &&
                    scene.netOfPin({b, 0}, PinType::OUTPUT) ==
                        scene.netOfPin({latch, 1}, PinType::INPUT) &&
                    scene.netOfPin({latch, 0}, PinType::OUTPUT) ==
                        scene.netOfPin({q, 0}, PinType::INPUT) &&
                    scene.netOfPin({latch, 1}, PinType::OUTPUT) ==
                        scene.netOfPin({nq, 0}, PinType::INPUT) &&
                    scene.getLogicComponent(q)->getStateInPin(0) &&
                    !scene.getLogicComponent(nq)->getStateInPin(0),
                "Latch rotation detached wires, exchanged Q/~Q or reset retained state."
            );
            accepted(actions.restore(*result.change->before, scene.getRevision()));
            require(
                scene.getCommittedComponentView(latch)->getOrientation() == previous,
                "Undo lost latch orientation."
            );
            accepted(actions.restore(*result.change->after, scene.getRevision()));
            require(
                scene.getCommittedComponentView(latch)->getOrientation() == direction,
                "Redo lost latch orientation."
            );
        }
    }
}

void bodyPlacement()
{
    Scene scene;
    EditorActions actions(scene);
    accepted(actions.apply({CreateInput{{0, 0}, {0.15f, 0.15f}, "inputPin"}}));
    const auto revision = scene.getRevision();
    const auto strict = actions.apply(
        {CreateInput{{1, 0}, {0.15f, 0.15f}, "inputPin", {}, PlacementPolicy::RejectOverlap}}
    );
    require(
        strict.error == EditError::Overlap && scene.getRevision() == revision,
        "Explicit strict placement stopped rejecting body overlap atomically."
    );
    const int second = accepted(actions.apply({CreateInput{{1, 0}, {0.15f, 0.15f}, "inputPin"}}))
                           .createdComponentIds.front();
    require(scene.checkOverlap(second), "Default creation did not permit overlapping bodies.");
    accepted(actions.apply({MoveComponent{second, {0, 1}}}));
    const auto movedRevision = scene.getRevision();
    require(
        scene.checkOverlap(second) &&
            actions.apply({MoveComponent{second, {0, 0}}}).error == EditError::Overlap &&
            scene.getCommittedComponentView(second)->getGridPosition() == GridCoords{0, 1} &&
            scene.getRevision() == movedRevision,
        "Move rejected body overlap or allowed an occupied center."
    );
    auto enlarged = currentLayout(scene, second);
    enlarged.size.x = 0.25f;
    const auto builds = scene.getTopologyBuildCount();
    accepted(actions.apply({ConfigureComponent{second, enlarged}}));
    require(
        scene.getCommittedComponentView(second)->getSize().x == 0.25f &&
            scene.getTopologyBuildCount() == builds + 1 && scene.checkOverlap(second),
        "Layout editing rejected body overlap or failed to rebuild once."
    );
    const auto stable = scene.getRevision();
    const auto batch =
        actions.apply({ConfigureInput{0, true}, CreateInput{{0, 0}, {0.15f, 0.15f}, "inputPin"}});
    require(
        batch.error == EditError::Overlap && !scene.getLogicComponent(0)->getStateOutPin() &&
            scene.getRevision() == stable,
        "A shared-center batch partially changed runtime state."
    );

    Scene automatic;
    automatic.addComponent(BuiltinComponentIds::Input, {0, 0});
    const auto free = accepted(EditorActions(automatic).apply(
        {CreateComponent{BuiltinComponentIds::Input, {0, 0}, {}, PlacementPolicy::FindFree}}
    ));
    require(
        automatic.getCommittedComponentView(free.createdComponentIds.front())->getGridPosition() ==
                GridCoords{1, -1} &&
            automatic.checkOverlap(free.createdComponentIds.front()),
        "Automatic placement moved beyond the next free center to avoid body overlap."
    );
    const auto chosen = accepted(EditorActions(automatic).apply(
        {CreateComponent{BuiltinComponentIds::And, {0, 1}, {}, PlacementPolicy::FindFree}}
    ));
    require(
        automatic.getCommittedComponentView(chosen.createdComponentIds.front())
                ->getGridPosition() == GridCoords{0, 1},
        "Automatic placement moved an already free center."
    );

    // Palette/default factory placement must use the same rule for gates and boxes.
    for (const auto& definition : nativeDefinitions())
    {
        Scene adjacent;
        adjacent.addComponent(definition.identity.id, {0, 0});
        const int next = adjacent.addComponent(definition.identity.id, {0, 1});
        const auto nextRevision = adjacent.getRevision();
        require(
            adjacent.getCommittedComponentView(next)->getGridPosition() == GridCoords{0, 1} &&
                EditorActions(adjacent).apply(
                                           {CreateComponent{definition.identity.id, {0, 1}}}
                ).error == EditError::Overlap &&
                adjacent.getRevision() == nextRevision,
            "A native component rejected adjacency or permitted identical centers."
        );
    }
    Scene pins;
    pins.addComponent(BuiltinComponentIds::Input, {0, 0}, {.inputState = true});
    const int output = pins.addComponent(BuiltinComponentIds::Output, {6, 0});
    const auto pinRevision = pins.getRevision();
    const auto rejected =
        EditorActions(pins).apply({CreateComponent{BuiltinComponentIds::Output, {2, 0}}});
    require(
        rejected.error == EditError::Overlap && pins.getRevision() == pinRevision &&
            pins.getComponentCount() == 2,
        "Creation accepted coincident pins at different component centers."
    );
    require(
        EditorActions(pins).apply({MoveComponent{output, {2, 0}}}).error == EditError::Overlap &&
            pins.getRevision() == pinRevision &&
            pins.getCommittedComponentView(output)->getGridPosition() == GridCoords{6, 0},
        "Movement accepted coincident pins or partially committed."
    );
    const auto freePins = accepted(EditorActions(pins).apply(
        {CreateComponent{BuiltinComponentIds::Output, {2, 0}, {}, PlacementPolicy::FindFree}}
    ));
    require(
        pins.getCommittedComponentView(freePins.createdComponentIds.front())->getGridPosition() ==
            GridCoords{3, -1},
        "Automatic placement ignored shared pin positions."
    );

    Scene rotated;
    rotated.addComponent(BuiltinComponentIds::Input, {0, 0});
    const int inverter = rotated.addComponent(BuiltinComponentIds::Not, {1, -1});
    const auto rotationRevision = rotated.getRevision();
    require(
        EditorActions(rotated)
                    .apply({ConfigureComponentProperties{
                        .componentId = inverter, .orientation = Orientation::Up
                    }})
                    .error == EditError::Overlap &&
            rotated.getRevision() == rotationRevision &&
            rotated.getCommittedComponentView(inverter)->getOrientation() == Orientation::Right,
        "Rotation placed an output pin over an existing pin."
    );

    Scene resized;
    const int gate = resized.addComponent(BuiltinComponentIds::And, {0, 0});
    resized.addComponent(BuiltinComponentIds::Input, {-3, 0});
    const auto resizeRevision = resized.getRevision();
    require(
        EditorActions(resized)
                    .apply({ConfigureComponentProperties{.componentId = gate, .inputCount = 3}})
                    .error == EditError::Overlap &&
            resized.getRevision() == resizeRevision &&
            resized.getLogicComponent(gate)->getInputPinCount() == 2,
        "Growing an input bank placed a new pin over an existing pin."
    );

    Scene touching;
    const glm::vec2 size{0.1f * 1.3f / 0.84f, 0.15f};
    accepted(EditorActions(touching).apply(
        {CreateInput{{0, 0}, size, "inputPin", {}, PlacementPolicy::RejectOverlap},
         CreateInput{{2, 0}, size, "inputPin", {}, PlacementPolicy::RejectOverlap}}
    ));
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
            {"editor_component_properties", componentProperties},
            {"gate_input_inversion", inputInversion},
            {"gate_orientation",
             []
             {
                 gateOrientation();
                 latchOrientation();
             }},
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
