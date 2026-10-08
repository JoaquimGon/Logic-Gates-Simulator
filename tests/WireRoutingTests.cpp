#include "Components/Definitions/NativeDefinitions.h"
#include "Editor/Actions/EditorActions.h"
#include "Editor/Scene.h"
#include "Geometry/GridMetrics.h"
#include "Geometry/WireRouting.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace
{
void require(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}

EditResult edit(Scene& scene, EditBatch batch)
{
    auto result = EditorActions(scene).apply(batch);
    if (!result)
        throw std::runtime_error(result.message);
    return result;
}

void clearance()
{
    ComponentGeometry obstacle{1, {0, 0}, 0, 0, 0.2f, 0.2f, {}};
    // Pin extent sticks out farther than the body and must receive clearance too.
    obstacle.pins.push_back({{1, 0}, PinType::OUTPUT, {4, 0}});
    const auto path = routeWire({-10, 0}, {10, 0}, {&obstacle, 1}, {});
    require(
        path && path->front() == GridCoords{-10, 0} && path->back() == GridCoords{10, 0},
        "Routing lost endpoints or failed to go around the obstacle."
    );
    for (std::size_t i = 1; i < path->size(); ++i)
    {
        auto a = (*path)[i - 1], b = (*path)[i];
        require(a.x == b.x || a.y == b.y, "Route left the orthogonal grid.");
        const int steps = std::abs(a.x - b.x) + std::abs(a.y - b.y);
        for (int j = 0; j <= steps; ++j)
        {
            GridCoords p{
                a.x + (b.x > a.x   ? j
                       : b.x < a.x ? -j
                                   : 0),
                a.y + (b.y > a.y   ? j
                       : b.y < a.y ? -j
                                   : 0)
            };
            require(
                p.x <= -3 || p.x >= 5 || p.y <= -3 || p.y >= 3,
                "Route crossed the body/pins or their one-cell padding."
            );
        }
    }
    auto access = routeWire({4, 0}, {10, 0}, {&obstacle, 1}, {});
    require(
        access && access->front() == GridCoords{4, 0},
        "Endpoint pin could not leave its own padding."
    );
    Wire attached;
    attached.setPath({{4, 0}, {8, 0}});
    attached.setNet(2);
    require(
        routeWire({10, 4}, {4, 0}, {&obstacle, 1}, {{0, attached}}, 1).has_value(),
        "An explicit connection to an already-wired pin was rejected."
    );
    require(
        !routeWire({0, 0}, {10, 0}, {&obstacle, 1}, {}),
        "A non-pin endpoint inside a body was accepted."
    );
    auto distant = routeWire(
        {std::numeric_limits<int>::min(), 0}, {std::numeric_limits<int>::max(), 0}, {}, {}
    );
    require(
        distant && distant->size() == 2,
        "Distant straight route overflowed or required grid search."
    );
    Wire other;
    other.setPath({{0, -2}, {0, 2}});
    other.setNet(2);
    const auto crossing = routeWire({-4, 0}, {4, 0}, {}, {{0, other}}, 1);
    require(crossing.has_value(), "Existing separate wire could not be routed around.");
    Wire routed;
    routed.setPath(*crossing);
    require(!routed.containsPoint({0, 0}), "Rerouting joined a separate wire net.");
}

void moves()
{
    Scene scene;
    const int source =
        scene.addComponent(BuiltinComponentIds::Input, {-12, 0}, {.inputState = true});
    const int gate = scene.addComponent(BuiltinComponentIds::Not, {0, 0});
    const int output = scene.addComponent(BuiltinComponentIds::Output, {0, 8});
    edit(scene, {AddWire{{{-11, 0}, {-2, 0}}}, AddWire{{{-6, 0}, {-6, 8}, {-1, 8}}}});
    const Scene original(scene);
    const auto builds = scene.getTopologyBuildCount();
    auto result = edit(scene, {MoveComponent{gate, {8, 4}}});
    scene.propagate();
    require(
        scene.getTopologyBuildCount() == builds + 1 && result.change &&
            scene.netOfPin({gate, 0}, PinType::INPUT) ==
                scene.netOfPin({source, 0}, PinType::OUTPUT) &&
            scene.netOfPin({output, 0}, PinType::INPUT) ==
                scene.netOfPin({source, 0}, PinType::OUTPUT),
        "Movement broke an attachment/branch or rebuilt more than once."
    );
    bool junction = false;
    for (const auto& [id, wire] : scene.getWires())
        junction |= wire.containsPoint({-6, 0});
    require(junction, "Rerouting moved a fixed branch junction.");
    require(
        static_cast<bool>(
            EditorActions(scene).restore(*result.change->before, scene.getRevision())
        ),
        "Move and route could not be restored together."
    );
    require(
        scene.getCommittedComponentView(gate)->getGridPosition() == GridCoords{0, 0} &&
            scene.wireCount() == original.wireCount(),
        "Undo did not restore original placement/wires."
    );
    edit(scene, {MoveComponent{gate, {8, 4}, false}});
    require(
        scene.netOfPin({gate, 0}, PinType::INPUT) == INVALID_NET_ID,
        "Disabled routing retained an attachment at the old position."
    );
    edit(scene, {MoveComponent{gate, {0, 0}, false}});
    require(
        scene.netOfPin({gate, 0}, PinType::INPUT) != INVALID_NET_ID,
        "Fixed-wire drop did not reconnect to matching geometry."
    );

    Scene latchScene;
    const int latch = latchScene.addComponent(BuiltinComponentIds::SrLatch, {0, 0});
    const int q = latchScene.addComponent(BuiltinComponentIds::Output, {16, 1});
    const int nq = latchScene.addComponent(BuiltinComponentIds::Output, {16, -5});
    edit(latchScene, {AddWire{{{3, 1}, {15, 1}}}, AddWire{{{3, -1}, {8, -1}, {8, -5}, {15, -5}}}});
    edit(latchScene, {MoveComponent{latch, {0, -2}}});
    edit(latchScene, {MoveComponent{latch, {4, 8}}});
    require(
        latchScene.netOfPin({latch, 0}, PinType::OUTPUT) ==
                latchScene.netOfPin({q, 0}, PinType::INPUT) &&
            latchScene.netOfPin({latch, 1}, PinType::OUTPUT) ==
                latchScene.netOfPin({nq, 0}, PinType::INPUT) &&
            latchScene.netOfPin({q, 0}, PinType::INPUT) !=
                latchScene.netOfPin({nq, 0}, PinType::INPUT),
        "Movement swapped or shorted independent output pins."
    );
}

void rollback()
{
    const ComponentGeometry blocked{1, {0, 0}, 0, 0, 1, 1, {}};
    std::vector<ComponentGeometry> before{
        {0, {-10, 0}, -0.5f, 0, 0.05f, 0.05f, {{{0, 0}, PinType::OUTPUT, {-9, 0}}}}, blocked
    };
    auto after = before;
    after[0].origin = {0, 0};
    after[0].pins[0].position = {1, 0};
    Wire wire;
    wire.setPath({{-9, 0}, {-5, 0}});
    wire.setNet(1);
    std::map<WireId, Wire> wires{{0, wire}};
    const std::array<int, 1> moved{0};
    require(
        !rerouteMovedWires(before, after, moved, wires) && wires.at(0).getPath() == wire.getPath(),
        "Failed routing partially changed a wire."
    );
    Scene scene;
    const int source = scene.addComponent(BuiltinComponentIds::Input, {-10, 0});
    scene.addComponent(BuiltinComponentIds::Output, {10, 0});
    edit(scene, {AddWire{{{-9, 0}, {9, 0}}}});
    scene.addComponent(BuiltinComponentIds::Output, {3, 0});
    const auto revision = scene.getRevision();
    const auto blockedMove = EditorActions(scene).apply({MoveComponent{source, {0, 0}}});
    require(
        blockedMove.error == EditError::InvalidWire && !blockedMove.change &&
            scene.getRevision() == revision &&
            scene.getCommittedComponentView(source)->getGridPosition() == GridCoords{-10, 0},
        "Insufficient clearance did not reject the complete move without mutation."
    );
    const auto result = EditorActions(scene).apply({MoveComponent{source, {10, 0}}});
    require(
        !result && !result.change && scene.getRevision() == revision &&
            scene.getCommittedComponentView(source)->getGridPosition() == GridCoords{-10, 0},
        "Rejected move changed the committed circuit."
    );
}
} // namespace

int main(int argc, char** argv)
{
    try
    {
        const std::pair<const char*, void (*)()> tests[] = {
            {"wire_routing_clearance", clearance},
            {"wire_routing_moves", moves},
            {"wire_routing_rollback", rollback}
        };
        for (const auto& [name, run] : tests)
            if (argc == 1 || std::string(argv[1]) == name)
            {
                run();
                std::cout << "PASS: " << name << '\n';
            }
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
