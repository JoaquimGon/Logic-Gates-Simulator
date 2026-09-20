#pragma once
#include <vector>
#include <map>
#include <unordered_map>
#include <optional>
#include <utility>
#include <glm/glm.hpp>
#include <cmath>
#include <algorithm>
#include "..\Views\GateView.h"
#include "..\Logic\Wire.h"
#include "..\Views\GridSystem.h"
#include "..\Views\InputPinView.h"
#include "../Logic/Circuit.h"

enum class HitType { NONE, COMPONENT_PIN, COMPONENT_BODY, WIRE_START, WIRE_END, WIRE_BODY, WIRE_JUNCTION};

struct HitResult {
    HitType type = HitType::NONE;
    int componentId = -1;              // valid for COMPONENT_PIN, COMPONENT_BODY
    int pinIndex = -1;                 // valid for COMPONENT_PIN
    PinType pinType = PinType::INPUT;  // valid for COMPONENT_PIN
    int wireId = INVALID_WIRE_ID;      // valid for WIRE_*
};

class Scene
{
public:
    // ----- Gates (the only concrete component type today) -----
    int addGate(GateType type, GridCoords gridPos, glm::vec2 size, const std::string& shaderName,
        std::vector<PinUI> inputs, std::vector<PinUI> outputs);

    int addInputPin(GridCoords gridPos, glm::vec2 size, const std::string& shaderName, bool initialState = false);


    void removeComponent(int componentId);
    ComponentView* getComponentView(int componentId);
    Component* getLogicComponent(int componentId);
    const std::unordered_map<int, std::unique_ptr<ComponentView>>& getComponentViewMap() const { return m_componentViews; }

    // ----- Wires (Input never touches this container's internals) -----
    // Wires are stored by a stable WireId rather than by a container index. Splitting,
    // merging and healing reshape the container, and an index cached on an earlier
    // frame would silently resolve to a different wire - or to no wire at all.
    size_t wireCount() const { return m_wires.size(); }
    Wire* getWire(WireId id);
    const Wire* getWire(WireId id) const;

    // Ordered by id, i.e. by creation order, which also keeps the draw order stable.
    const std::map<WireId, Wire>& getWires() const { return m_wires; }

    // Ids of every stored wire, for callers that walk the container while reshaping it.
    std::vector<WireId> getWireIds() const;

    std::optional<WireId>     commitWire(Wire wire);
    std::optional<Wire>       extractWire(WireId id);
    bool                      splitWireAt(WireId id, GridCoords point, Wire& outA, Wire& outB);
    std::pair<WireId, WireId> addWires(Wire a, Wire b);
    bool                      removeWire(WireId id);

    std::vector<glm::vec3> getWireIntersections() const;
    bool getCollinearOverlap(GridCoords a, GridCoords b, GridCoords c, GridCoords d, GridCoords& outStart, GridCoords& outEnd) const;
    void forceEndpointAt(GridCoords p);
    void reconnectWiresToComponent(int componentId);
    void healWires();


    // ----- Logic connections -----
    bool connectPins(int srcComponentId, int destComponentId, int destPinIndex);
    void disconnectPins(int srcComponentId, int destComponentId, int destPinIndex);

    // ----- Hit-testing -----
    HitResult hitTest(glm::vec2 worldPos, GridCoords gridPos) const;

    // ----- Simulation -----
    // Forwards the simulation status (e.g. a detected combinational loop) so callers
    // can report it without catching exceptions.
    EvalOrderResult propagate();
    void syncVisuals();
    bool handleClick(int componentId);
    bool checkOverlap(int draggedComponentId) const;
private:
    Circuit m_circuit;
    std::unordered_map<int, std::unique_ptr<ComponentView>> m_componentViews;

    // Keyed by a stable WireId; see the note above the wire API.
    std::map<WireId, Wire> m_wires;
    WireId m_nextWireId = 0;

    /*
    * @brief Stores a wire under a freshly allocated id, without validating its path.
    * @param wire The wire to store.
    * @return The id the wire was stored under.
    */
    WireId insertWire(Wire wire);

};