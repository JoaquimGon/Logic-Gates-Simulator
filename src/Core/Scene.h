#pragma once
#include "../Logic/Circuit.h"
#include "..\Logic\Net.h"
#include "..\Logic\Wire.h"
#include "..\Views\ClockView.h"
#include "..\Views\GateView.h"
#include "..\Views\GridSystem.h"
#include "..\Views\InputPinView.h"
#include "..\Views\LatchView.h"

#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>
#include <map>
#include <optional>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

enum class HitType
{
    NONE,
    COMPONENT_PIN,
    COMPONENT_BODY,
    WIRE_START,
    WIRE_END,
    WIRE_BODY,
    WIRE_JUNCTION
};

struct HitResult
{
    HitType type = HitType::NONE;
    int componentId = -1;             // valid for COMPONENT_PIN, COMPONENT_BODY
    int pinIndex = -1;                // valid for COMPONENT_PIN
    PinType pinType = PinType::INPUT; // valid for COMPONENT_PIN
    int wireId = INVALID_WIRE_ID;     // valid for WIRE_*
};

class Scene
{
  public:
    /**
     * @brief Creates a gate whose logical input count matches the supplied visual pins.
     * @param type Gate operation; NOT requires one input, other gates at least two.
     * @param gridPos Component origin on the grid.
     * @param size Visual footprint in world units.
     * @param shaderName Registered shader for the gate.
     * @param inputs Unique input indices from zero to input count minus one, in any order.
     * @param outputs Exactly one output, with index zero.
     * @return Created component ID. Invalid layouts throw std::invalid_argument.
     */
    int addGate(
        GateType type,
        GridCoords gridPos,
        glm::vec2 size,
        const std::string& shaderName,
        std::vector<PinUI> inputs,
        std::vector<PinUI> outputs
    );

    int addInputPin(
        GridCoords gridPos, glm::vec2 size, const std::string& shaderName, bool initialState = false
    );

    int addLatch(LatchType type, GridCoords gridPos);

    void removeComponent(int componentId);
    ComponentView* getComponentView(int componentId);
    Component* getLogicComponent(int componentId);

    const std::unordered_map<int, std::unique_ptr<ComponentView>>& getComponentViewMap() const
    {
        return m_componentViews;
    }

    // ----- Wires: pure geometry (the electrical side lives in Net) -----
    // Wires are stored by a stable WireId rather than by a container index.
    // Splitting and merging reshape the container, and an index cached on an
    // earlier frame would silently resolve to a different wire - or to no wire
    // at all.
    size_t wireCount() const { return m_wires.size(); }

    Wire* getWire(WireId id);
    const Wire* getWire(WireId id) const;

    // Ordered by id, i.e. by creation order, which also keeps the draw order
    // stable.
    const std::map<WireId, Wire>& getWires() const { return m_wires; }

    // Ids of every stored wire, for callers that walk the container while
    // reshaping it.
    std::vector<WireId> getWireIds() const;

    // Each of these re-derives the nets: geometry is the only thing callers
    // edit, and rebuildNets() is what turns that geometry into connectivity and
    // Circuit edges.
    std::optional<WireId> commitWire(Wire wire);
    std::optional<Wire> extractWire(WireId id);
    bool splitWireAt(WireId id, GridCoords point, Wire& outA, Wire& outB);
    std::pair<WireId, WireId> addWires(Wire a, Wire b);
    bool removeWire(WireId id);

    std::vector<glm::vec3> getWireIntersections() const;
    bool getCollinearOverlap(
        GridCoords a,
        GridCoords b,
        GridCoords c,
        GridCoords d,
        GridCoords& outStart,
        GridCoords& outEnd
    ) const;

    // ----- Nets: the single source of truth for connectivity -----
    size_t netCount() const { return m_nets.size(); }

    const std::map<NetId, Net>& getNets() const { return m_nets; }

    const Net* getNet(NetId id) const;
    NetId netOfWire(WireId id) const;
    NetId netOfPin(const PinRef& pin, PinType type) const;

    /*
     * @brief The state a wire drawn from this pin would show right now.
     * An output pin is a driver, so it reports its own value even before any
     * geometry has been drawn from it; an input pin reports whatever drives the
     * net it hangs off.
     */
    PinState pinState(const PinRef& pin, PinType type);

    /*
     * @brief Re-derives every net, and the Circuit edges that follow from them.
     * Run after every geometry or component edit, never per frame: a net is one
     * connected component of the wires that share an endpoint, so this is a
     * graph walk over the wires rather than the geometric fixpoint over every
     * pair of segments the old code needed.
     */
    void rebuildNets();

    // ----- Hit-testing -----
    HitResult hitTest(glm::vec2 worldPos, GridCoords gridPos) const;

    // ----- Simulation -----
    // Forwards the simulation status (e.g. a detected combinational loop) so
    // callers can report it without catching exceptions.
    EvalOrderResult propagate();
    void syncVisuals();
    bool handleClick(int componentId);
    bool checkOverlap(int draggedComponentId) const;

    bool updateClocks(float deltaTime);

    bool isSimulationDirty() const { return m_circuit.isStateDirty(); }

    void markSimulationDirty() { m_circuit.markStateDirty(); }

    void togglePauseAllClocks();
    void stepAllClocks();
    void setAllClocksFrequency(float hz);

    int addClock(
        GridCoords gridPos, glm::vec2 size, const std::string& shaderName, float frequencyHz = 1.0f
    );

    // Debugging
    float getLastPropagateTimeMs() const { return m_circuit.getLastPropagateTimeMs(); }

    EvalOrderResult getLastEvalResult() const { return m_circuit.getLastEvalResult(); }

    size_t getEvalOrderSize() const { return m_circuit.getEvalOrderSize(); }

    size_t getComponentCount() const { return m_circuit.getComponentCount(); }

    size_t getShortedNetCount() const
    {
        size_t count = 0;
        for (const auto& [id, net] : m_nets)
        {
            if (net.shorted())
                count++;
        }
        return count;
    }

  private:
    Circuit m_circuit;
    std::unordered_map<int, std::unique_ptr<ComponentView>> m_componentViews;

    // Keyed by a stable WireId; see the note above the wire API.
    std::map<WireId, Wire> m_wires;
    WireId m_nextWireId = 0;

    // The nets, and the pin -> net index that makes a pin lookup O(log n). Both
    // are rebuilt from the geometry by rebuildNets(); Wire::getNet() is the
    // per-wire reverse lookup. Nothing else is allowed to own connectivity.
    std::map<NetId, Net> m_nets;
    NetId m_nextNetId = 0;

    // (componentId, pinIndex, isOutput) -> net. The direction belongs in the
    // key because a component's input and output pins have separate index
    // spaces that overlap.
    std::map<std::tuple<int, int, bool>, NetId> m_pinNet;

    /*
     * @brief Stores a wire under a freshly allocated id, without validating its
     * path.
     * @param wire The wire to store.
     * @return The id the wire was stored under.
     */
    WireId insertWire(Wire wire);

    // Geometry surgery that deliberately leaves the nets untouched, so that the
    // steps of rebuildNets() can reshape the container while they collect. The
    // public mutators above wrap these and re-derive the nets afterwards.
    std::pair<WireId, WireId> insertWires(Wire a, Wire b);
    bool splitWireGeometry(WireId id, GridCoords point, Wire& outA, Wire& outB);
    void dropDegenerateWires();

    // The three steps of rebuildNets(), split apart so each one stays readable.
    void settleGeometry();
    void collectNets();
    void emitNetEdges();
};