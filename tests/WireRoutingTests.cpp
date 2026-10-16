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


/** @brief Verify route endpoints and detours respect visible component bounds with the intended half-cell clearance. */
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
            GridCoords p{a.x + (b.x > a.x ? j : b.x < a.x ? -j : 0), a.y + (b.y > a.y ? j : b.y < a.y ? -j : 0)};
            require(
                p.x <= -3 || p.x >= 5 || p.y <= -3 || p.y >= 3,
                "Route crossed the body/pins or their half-cell padding."
            );
        }
    }
    // Actual source/bulb bounds sit near a grid threshold: the clock must not
    // acquire an extra blocked row just because its body is slightly wider.
    for (const auto* definition : {BuiltinComponentIds::Clock, BuiltinComponentIds::Input, BuiltinComponentIds::Output})
    {
        Scene scene;
        scene.addComponent(definition, {0, 0});
        const auto geometry = scene.committedGeometry();
        require(
            routeWire({-6, 2}, {6, 2}, geometry, {}) == std::optional{std::vector<GridCoords>{{-6, 2}, {6, 2}}},
            "Source/bulb routing still blocked the second grid row outside the body."
        );
        for (const bool reverse : {false, true})
        {
            const GridCoords start = reverse ? GridCoords{-6, 0} : GridCoords{-2, 0};
            const GridCoords end = reverse ? GridCoords{-2, 0} : GridCoords{-6, 0};
            require(
                routeWire(start, end, geometry, {}) == std::optional{std::vector<GridCoords>{start, end}},
                "A wire could not start/end two cells from a source/bulb centre."
            );
        }
        const auto around = routeWire({-6, 0}, {6, 0}, geometry, {});
        require(around.has_value(), "Could not reroute around a source/bulb body.");
        for (const auto point : *around)
            require(std::abs(point.y) <= 2, "Source/bulb detour left an excessive vertical gap.");
        require(
            !routeWire({-1, 1}, {-6, 1}, geometry, {}) && !routeWire({-6, 1}, {-1, 1}, geometry, {}),
            "Reduced clearance allowed a non-pin endpoint too close to a source/bulb body."
        );
    }
    auto access = routeWire({4, 0}, {10, 0}, {&obstacle, 1}, {});
    require(access && access->front() == GridCoords{4, 0}, "Endpoint pin could not leave its own padding.");
    Wire attached;
    attached.setPath({{4, 0}, {8, 0}});
    attached.setNet(2);
    require(
        routeWire({10, 4}, {4, 0}, {&obstacle, 1}, {{0, attached}}, 1).has_value(),
        "An explicit connection to an already-wired pin was rejected."
    );
    require(!routeWire({0, 0}, {10, 0}, {&obstacle, 1}, {}), "A non-pin endpoint inside a body was accepted.");
    auto distant = routeWire({std::numeric_limits<int>::min(), 0}, {std::numeric_limits<int>::max(), 0}, {}, {});
    require(distant && distant->size() == 2, "Distant straight route overflowed or required grid search.");
    Wire other;
    other.setPath({{0, -2}, {0, 2}});
    other.setNet(2);
    const auto crossing = routeWire({-4, 0}, {4, 0}, {}, {{0, other}}, 1);
    require(crossing.has_value(), "A route could not cross an existing wire.");
    Wire routed;
    routed.setPath(*crossing);
    require(*crossing == std::vector<GridCoords>{{-4, 0}, {4, 0}}, "An interior crossing still forced a detour.");
    Scene separate;
    edit(separate, {AddWire{other.getPath()}, AddWire{*crossing}});
    require(
        separate.netCount() == 2 && separate.getWireIntersections().empty(),
        "An interior crossing unexpectedly created a junction."
    );
    // An internal vertex also stays a visual crossing, not a whole-wire endpoint.
    other.setPath({{0, -2}, {0, 0}, {0, 2}, {2, 2}});
    require(routeWire({-4, 0}, {4, 0}, {}, {{0, other}}, 1) == crossing, "An interior vertex forced a detour.");
    other.setPath({{0, 0}, {0, 3}});
    const auto endpoint = routeWire({-4, 0}, {4, 0}, {}, {{0, other}}, 1);
    require(endpoint.has_value(), "Could not route around another wire's endpoint.");
    routed.setPath(*endpoint);
    require(!routed.containsPoint({0, 0}), "Routing made an unintended junction at another wire's endpoint.");
    const auto join = routeWire({-4, 0}, {0, 0}, {}, {{0, other}}, 1);
    require(join && join->back() == GridCoords{0, 0}, "Explicitly joining a wire endpoint was blocked.");
    other.setPath({{-1, 0}, {1, 0}});
    const auto overlap = routeWire({-4, 0}, {4, 0}, {}, {{0, other}}, 1);
    require(overlap.has_value(), "Could not route around a collinear span.");
    routed.setPath(*overlap);
    require(!routed.containsPoint({0, 0}), "Routing merged a collinear span of an unrelated net.");
}


/** @brief Verify attached-wire rerouting preserves driven connectivity while alternate movement retains fixed geometry.
 */
void moves()
{
    Scene scene;
    const int source = scene.addComponent(BuiltinComponentIds::Input, {-12, 0}, {.inputState = true});
    const int gate = scene.addComponent(BuiltinComponentIds::Not, {0, 0});
    const int output = scene.addComponent(BuiltinComponentIds::Output, {0, 8});
    edit(scene, {AddWire{{{-11, 0}, {-2, 0}}}, AddWire{{{-6, 0}, {-6, 8}, {-1, 8}}}});
    const Scene original(scene);
    const auto builds = scene.getTopologyBuildCount();
    auto result = edit(scene, {MoveComponent{gate, {8, 4}}});
    scene.propagate();
    require(
        scene.getTopologyBuildCount() == builds + 1 && result.change &&
            scene.netOfPin({gate, 0}, PinType::INPUT) == scene.netOfPin({source, 0}, PinType::OUTPUT) &&
            scene.netOfPin({output, 0}, PinType::INPUT) == scene.netOfPin({source, 0}, PinType::OUTPUT),
        "Movement broke an attachment/branch or rebuilt more than once."
    );
    bool junction = false;
    for (const auto& [id, wire] : scene.getWires())
        junction |= wire.containsPoint({-6, 0});
    require(junction, "Rerouting moved a fixed branch junction.");
    require(
        static_cast<bool>(EditorActions(scene).restore(*result.change->before, scene.getRevision())),
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
        latchScene.netOfPin({latch, 0}, PinType::OUTPUT) == latchScene.netOfPin({q, 0}, PinType::INPUT) &&
            latchScene.netOfPin({latch, 1}, PinType::OUTPUT) == latchScene.netOfPin({nq, 0}, PinType::INPUT) &&
            latchScene.netOfPin({q, 0}, PinType::INPUT) != latchScene.netOfPin({nq, 0}, PinType::INPUT),
        "Movement swapped or shorted independent output pins."
    );
}


/** @brief Verify a failed route leaves complete scene geometry and transaction state unchanged. */
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
        blockedMove.error == EditError::InvalidWire && !blockedMove.change && scene.getRevision() == revision &&
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


/** @brief Verify component landings trim only dangling body-side tails and preserve downstream pins and junctions. */
void landings()
{
    {
        Scene geometry;
        const int component = geometry.addComponent(BuiltinComponentIds::Output, {0, 0});
        Wire main, branch;
        main.setPath({{-6, 0}, {6, 0}});
        branch.setPath({{4, 0}, {4, 5}});
        main.setNet(1);
        branch.setNet(1);
        std::map<WireId, Wire> wires{{0, main}, {1, branch}};
        WireId nextId = 2;
        const std::array<int, 1> moved{component};
        require(
            fitWiresToComponents(geometry.committedGeometry(), moved, wires, nextId),
            "A new junction from attached-wire rerouting prevented a safe landing."
        );
        int degree = 0;
        bool tail = false;
        for (const auto& [id, wire] : wires)
        {
            degree += wire.getPath().front() == GridCoords{4, 0};
            degree += wire.getPath().back() == GridCoords{4, 0};
            tail |= wire.containsPoint({6, 0});
            require(!wire.containsPoint({0, 0}), "A junction continuation stayed inside the body.");
        }
        require(degree == 3 && tail, "Landing deleted or bypassed a junction before normalization.");
    }
    for (const bool fromLeft : {false, true})
    {
        Scene scene;
        const int source = scene.addComponent(BuiltinComponentIds::Input, {fromLeft ? -10 : 10, 4});
        const int gate = scene.addComponent(BuiltinComponentIds::Not, {0, 8});
        const int sourceX = fromLeft ? -9 : 11;
        edit(scene, {AddWire{{{sourceX, 4}, {sourceX, 0}, {fromLeft ? 8 : -8, 0}}}});
        edit(scene, {MoveComponent{gate, {0, 0}}});
        require(
            scene.netOfPin({gate, 0}, fromLeft ? PinType::INPUT : PinType::OUTPUT) ==
                    scene.netOfPin({source, 0}, PinType::OUTPUT) &&
                scene.netOfPin({gate, 0}, fromLeft ? PinType::OUTPUT : PinType::INPUT) == INVALID_NET_ID,
            "A second newly landed gate pin falsely protected the dangling body-side tail."
        );
        for (const auto& [id, wire] : scene.getWires())
            require(!wire.containsPoint({0, 0}), "The tail remained inside a gate after landing.");
    }
    for (const bool reverse : {false, true})
    {
        Scene scene;
        const int source = scene.addComponent(BuiltinComponentIds::Input, {-10, 0}, {.inputState = true});
        const int sink = scene.addComponent(BuiltinComponentIds::Output, {0, 6});
        std::vector<GridCoords> path{{-9, 0}, {8, 0}};
        if (reverse)
            std::reverse(path.begin(), path.end());
        edit(scene, {AddWire{path}});
        const auto builds = scene.getTopologyBuildCount();
        const auto result = edit(scene, {MoveComponent{sink, {0, 0}}});
        scene.propagate();
        require(
            scene.wireCount() == 1 && scene.getWires().begin()->second.containsPoint({-1, 0}) &&
                !scene.getWires().begin()->second.containsPoint({0, 0}) &&
                !scene.getWires().begin()->second.containsPoint({8, 0}) &&
                scene.netOfPin({source, 0}, PinType::OUTPUT) == scene.netOfPin({sink, 0}, PinType::INPUT) &&
                scene.getLogicComponent(sink)->getStateInPin(0) && scene.getTopologyBuildCount() == builds + 1,
            "Landing did not trim only the body-side dangling tail and connect the pin."
        );
        require(
            static_cast<bool>(EditorActions(scene).restore(*result.change->before, scene.getRevision())) &&
                scene.getWires().begin()->second.getPath() == path &&
                scene.getCommittedComponentView(sink)->getGridPosition() == GridCoords{0, 6},
            "Undo did not restore the shortened tail and component together."
        );
        require(
            static_cast<bool>(EditorActions(scene).restore(*result.change->after, scene.getRevision())) &&
                !scene.getWires().begin()->second.containsPoint({8, 0}),
            "Redo did not restore the landing trim."
        );
    }
    for (const bool branch : {false, true})
    {
        Scene scene;
        const int source = scene.addComponent(BuiltinComponentIds::Input, {-10, 0}, {.inputState = true});
        const int landing = scene.addComponent(BuiltinComponentIds::Output, {0, 6});
        const int sink = scene.addComponent(BuiltinComponentIds::Output, {10, 0});
        edit(scene, {AddWire{{{-9, 0}, {branch ? 8 : 9, 0}}}});
        if (branch)
            edit(scene, {AddWire{{{4, 0}, {4, 5}}}});
        edit(scene, {MoveComponent{landing, {0, 0}}});
        scene.propagate();
        require(
            (!branch || scene.netOfPin({sink, 0}, PinType::INPUT) == INVALID_NET_ID) &&
                (branch || (scene.netOfPin({source, 0}, PinType::OUTPUT) == scene.netOfPin({sink, 0}, PinType::INPUT) &&
                            scene.getLogicComponent(sink)->getStateInPin(0))) &&
                scene.netOfPin({source, 0}, PinType::OUTPUT) == scene.netOfPin({landing, 0}, PinType::INPUT) &&
                scene.getLogicComponent(landing)->getStateInPin(0),
            "Landing deleted or disconnected a continuation with another pin or junction."
        );
        bool hasBranch = false, hasTail = false;
        for (const auto& [id, wire] : scene.getWires())
        {
            require(!wire.containsPoint({0, 0}), "Preserved continuation remained inside the moved body.");
            hasBranch |= wire.containsPoint({4, 5});
            hasTail |= wire.containsPoint({8, 0});
        }
        require(!branch || (hasBranch && hasTail), "Landing removed a junction or its dangling branch.");
    }
    {
        Scene scene;
        const int landing = scene.addComponent(BuiltinComponentIds::Output, {0, 6});
        edit(scene, {AddWire{{{-9, 0}, {8, 0}}}, AddWire{{{4, -5}, {4, 5}}}});
        edit(scene, {MoveComponent{landing, {0, 0}}});
        require(scene.netCount() == 2, "A visual crossing was treated as a connected branch.");
        bool crossing = false;
        for (const auto& [id, wire] : scene.getWires())
        {
            crossing |= wire.containsPoint({4, -5}) && wire.containsPoint({4, 5});
            require(!wire.containsPoint({8, 0}), "A visual crossing protected a dangling tail.");
        }
        require(crossing, "Trimming the tail changed a separate crossing wire.");
    }
    {
        Scene scene;
        const int clock = scene.addComponent(BuiltinComponentIds::Clock, {0, 6});
        edit(scene, {AddWire{{{-6, 0}, {6, 0}}}});
        edit(scene, {MoveComponent{clock, {0, 0}}});
        // Its output lands on the line; the left tail is body-side, the right tail stays.
        require(
            scene.wireCount() == 1 && scene.getWires().begin()->second.containsPoint({1, 0}) &&
                scene.getWires().begin()->second.containsPoint({6, 0}) &&
                !scene.getWires().begin()->second.containsPoint({-6, 0}),
            "A clock landing trimmed the outward tail instead of the body-side tail."
        );
    }
    {
        Scene scene;
        const int clock = scene.addComponent(BuiltinComponentIds::Clock, {0, 6});
        edit(scene, {AddWire{{{-6, 1}, {6, 1}}}});
        edit(scene, {MoveComponent{clock, {0, 0}}});
        const auto& wire = scene.getWires().begin()->second;
        require(
            wire.containsPoint({-6, 1}) && wire.containsPoint({6, 1}) && !wire.containsPoint({0, 1}),
            "Moving a body without a pin landing did not preserve and reroute the covered wire."
        );
    }
    {
        Scene scene;
        const int landing = scene.addComponent(BuiltinComponentIds::Output, {0, 6});
        edit(scene, {AddWire{{{-6, 0}, {6, 0}, {6, 4}, {-6, 4}, {-6, 0}}}});
        edit(scene, {MoveComponent{landing, {0, 0}}});
        bool loop = false, contact = false;
        for (const auto& [id, wire] : scene.getWires())
        {
            const auto& path = wire.getPath();
            loop |= path.front() == path.back();
            contact |= wire.containsPoint({-1, 0});
            require(!wire.containsPoint({0, 0}), "Landing left a loop inside the body.");
        }
        require(loop && contact && scene.netCount() == 1, "Landing deleted a loop or disconnected it from the pin.");
    }
    {
        Scene scene;
        const int clock = scene.addComponent(BuiltinComponentIds::Clock, {0, 6});
        edit(scene, {AddWire{{{-1, 1}, {1, 1}, {1, -1}, {-1, -1}, {-1, 1}}}});
        const auto revision = scene.getRevision();
        const auto path = scene.getWires().begin()->second.getPath();
        const auto result = EditorActions(scene).apply({MoveComponent{clock, {0, 0}}});
        require(
            !result && scene.getRevision() == revision && scene.getWires().begin()->second.getPath() == path &&
                scene.getCommittedComponentView(clock)->getGridPosition() == GridCoords{0, 6},
            "An unroutable loop was deleted or the failed landing partially committed."
        );
    }
    {
        Scene scene;
        const int clock = scene.addComponent(BuiltinComponentIds::Clock, {0, 6});
        edit(scene, {AddWire{{{-6, 0}, {6, 0}}}});
        edit(scene, {MoveComponent{clock, {0, 0}, false}});
        int length = 0;
        for (const auto& [id, wire] : scene.getWires())
        {
            const auto& fixed = wire.getPath();
            require(fixed.front().y == 0 && fixed.back().y == 0, "Alt movement rerouted a wire.");
            length += std::abs(fixed.back().x - fixed.front().x);
        }
        require(length == 12, "Alt movement trimmed a fixed wire rather than only splitting at its new pin.");
    }
}


/** @brief Verify newly created components fit existing wires with the same trimming and preservation rules as moved
 * components. */
void placements()
{
    {
        Scene scene;
        scene.addComponent(BuiltinComponentIds::Clock, {0, 0});
        const int source = scene.addComponent(BuiltinComponentIds::Input, {-10, -1}, {.inputState = true});
        edit(scene, {AddWire{{{-9, -1}, {8, -1}}}});
        const auto result =
            edit(scene, {CreateComponent{BuiltinComponentIds::Output, {0, 0}, {}, PlacementPolicy::FindFree}});
        const int sink = result.createdComponentIds.front();
        scene.propagate();
        require(
            scene.getCommittedComponentView(sink)->getGridPosition() == GridCoords{1, -1} &&
                scene.getWires().begin()->second.containsPoint({0, -1}) &&
                !scene.getWires().begin()->second.containsPoint({8, -1}) &&
                scene.netOfPin({source, 0}, PinType::OUTPUT) == scene.netOfPin({sink, 0}, PinType::INPUT) &&
                scene.getLogicComponent(sink)->getStateInPin(0),
            "FindFree trimmed at the requested position instead of the resolved landing pin."
        );
    }
    for (const bool reverse : {false, true})
    {
        Scene scene;
        const int source = scene.addComponent(BuiltinComponentIds::Input, {-10, 0}, {.inputState = true});
        std::vector<GridCoords> path{{-9, 0}, {8, 0}};
        if (reverse)
            std::reverse(path.begin(), path.end());
        edit(scene, {AddWire{path}});
        const auto builds = scene.getTopologyBuildCount();
        const auto result = edit(scene, {CreateComponent{BuiltinComponentIds::Output, {0, 0}}});
        const int sink = result.createdComponentIds.front();
        scene.propagate();
        require(
            scene.wireCount() == 1 && scene.getWires().begin()->second.containsPoint({-1, 0}) &&
                !scene.getWires().begin()->second.containsPoint({8, 0}) &&
                scene.netOfPin({source, 0}, PinType::OUTPUT) == scene.netOfPin({sink, 0}, PinType::INPUT) &&
                scene.getLogicComponent(sink)->getStateInPin(0) && scene.getTopologyBuildCount() == builds + 1,
            "New placement did not trim the dangling tail and connect in one rebuild."
        );
        require(
            static_cast<bool>(EditorActions(scene).restore(*result.change->before, scene.getRevision())) &&
                scene.getComponentCount() == 1 && scene.getWires().begin()->second.getPath() == path,
            "Undo did not remove the placed component and restore its trimmed tail together."
        );
        require(
            static_cast<bool>(EditorActions(scene).restore(*result.change->after, scene.getRevision())) &&
                scene.getLogicComponent(sink) && !scene.getWires().begin()->second.containsPoint({8, 0}),
            "Redo did not restore placement and trimming together."
        );
    }
    for (const bool fromLeft : {false, true})
    {
        Scene scene;
        const int source = scene.addComponent(BuiltinComponentIds::Input, {fromLeft ? -10 : 10, 4});
        const int sourceX = fromLeft ? -9 : 11;
        edit(scene, {AddWire{{{sourceX, 4}, {sourceX, 0}, {fromLeft ? 8 : -8, 0}}}});
        const int gate = scene.addComponent(BuiltinComponentIds::Not, {0, 0});
        require(
            scene.netOfPin({gate, 0}, fromLeft ? PinType::INPUT : PinType::OUTPUT) ==
                    scene.netOfPin({source, 0}, PinType::OUTPUT) &&
                scene.netOfPin({gate, 0}, fromLeft ? PinType::OUTPUT : PinType::INPUT) == INVALID_NET_ID,
            "Another pin on a new gate protected its body-side dangling tail."
        );
    }
    for (const bool branch : {false, true})
    {
        Scene scene;
        const int source = scene.addComponent(BuiltinComponentIds::Input, {-10, 0}, {.inputState = true});
        const int sink = scene.addComponent(BuiltinComponentIds::Output, {10, 0});
        edit(scene, {AddWire{{{-9, 0}, {branch ? 8 : 9, 0}}}});
        if (branch)
            edit(scene, {AddWire{{{4, 0}, {4, 5}}}});
        const int placed = scene.addComponent(BuiltinComponentIds::Output, {0, 0});
        scene.propagate();
        require(
            scene.netOfPin({source, 0}, PinType::OUTPUT) == scene.netOfPin({placed, 0}, PinType::INPUT) &&
                scene.getLogicComponent(placed)->getStateInPin(0) &&
                (branch || scene.getLogicComponent(sink)->getStateInPin(0)),
            "Placement disconnected a continuation attached to another pin."
        );
        bool hasBranch = false, hasTail = false;
        for (const auto& [id, wire] : scene.getWires())
        {
            require(!wire.containsPoint({0, 0}), "A preserved continuation remained inside the placed body.");
            hasBranch |= wire.containsPoint({4, 5});
            hasTail |= wire.containsPoint({8, 0});
        }
        require(!branch || (hasBranch && hasTail), "Placement deleted a junction or its continuation.");
    }
    {
        Scene scene;
        edit(scene, {AddWire{{{-6, 1}, {6, 1}}}});
        scene.addComponent(BuiltinComponentIds::Clock, {0, 0});
        const auto& wire = scene.getWires().begin()->second;
        require(
            wire.containsPoint({-6, 1}) && wire.containsPoint({6, 1}) && !wire.containsPoint({0, 1}),
            "Placement without a pin landing did not preserve and reroute the covered wire."
        );
    }
    {
        Scene scene;
        edit(scene, {AddWire{{{-1, 1}, {1, 1}, {1, -1}, {-1, -1}, {-1, 1}}}});
        const auto revision = scene.getRevision();
        const auto path = scene.getWires().begin()->second.getPath();
        const auto result = EditorActions(scene).apply({CreateComponent{BuiltinComponentIds::Clock, {0, 0}}});
        require(
            !result && !result.change && scene.getRevision() == revision && scene.getComponentCount() == 0 &&
                scene.getWires().begin()->second.getPath() == path,
            "Unroutable placement deleted a loop or partially committed."
        );
        require(
            scene.addComponent(BuiltinComponentIds::Clock, {10, 10}) == 0,
            "Failed placement consumed a component identity."
        );
    }
    {
        Scene scene;
        const std::vector<GridCoords> path{{-6, 0}, {6, 0}};
        edit(scene, {AddWire{path}});
        edit(scene, {CreateComponent{BuiltinComponentIds::Clock, {0, 0}, {}, PlacementPolicy::AllowOverlap}});
        int length = 0;
        for (const auto& [id, wire] : scene.getWires())
        {
            const auto& fixed = wire.getPath();
            require(fixed.front().y == 0 && fixed.back().y == 0, "Explicit import rerouted its authored wire.");
            length += std::abs(fixed.back().x - fixed.front().x);
        }
        require(length == 12, "Explicit import trimmed its authored wire geometry.");
    }
}
} // namespace


int main(int argc, char** argv)
{
    try
    {
        const std::pair<const char*, void (*)()> tests[] = {
            {"wire_routing_clearance", clearance},
            {"wire_routing_moves", moves},
            {"wire_routing_rollback", rollback},
            {"wire_routing_landings", landings},
            {"wire_routing_placements", placements}
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
