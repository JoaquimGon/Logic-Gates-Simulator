#pragma once
#include <vector>
#include <unordered_map>
#include <glm/glm.hpp>
#include "GateView.h"
#include "Wire.h"
#include "GridSystem.h"
#include "../Logic/Circuit.h"

enum class HitType { NONE, GATE_PIN, GATE_BODY, WIRE_START, WIRE_END, WIRE_BODY };

struct HitResult {
    HitType type = HitType::NONE;
    int     gateId = -1;   // valid for GATE_PIN, GATE_BODY
    int     pinIndex = -1; // valid for GATE_PIN
    PinType pinType = PinType::INPUT; // valid for GATE_PIN
    int     wireIndex = -1; // valid for WIRE_*
};

class Scene
{
public:
    // ----- Gates -----
    int addGate(GateType type, GridCoords gridPos, glm::vec2 size, const std::string& shaderName,
        std::vector<PinUI> inputs, std::vector<PinUI> outputs, bool outInverted = false);
    void removeGate(int gateId);
    GateView* getGateView(int gateId);
    Gate* getLogicGate(int gateId);
    const std::unordered_map<int, GateView>& getGateViewMap() const { return m_gateViews; }

    // ----- Wires (Input never touches this vector directly) -----
    size_t wireCount() const { return m_wires.size(); }
    Wire& wireAt(size_t index) { return m_wires[index]; }
    const std::vector<Wire>& getWires() const { return m_wires; }

    size_t commitWire(Wire wire);
    Wire   extractWire(size_t index);
    bool   splitWireAt(size_t index, GridCoords point, Wire& outA, Wire& outB);
    void   addWires(Wire a, Wire b);
    void   removeWire(size_t index);

    // Reattaches any dangling wire endpoints at this gate's pins after a drag, splitting
    // through wires if needed, and reconnects the logic layer automatically.
    void reconnectWiresToGate(int gateId);

    // ----- Logic connections -----
    bool connectPins(int srcGateId, int destGateId, int destPinIndex);
    void disconnectPins(int srcGateId, int destGateId, int destPinIndex);

    // ----- Hit-testing (the ONLY place that knows how to test pins/wires/gate bodies) -----
    HitResult hitTest(glm::vec2 worldPos, GridCoords gridPos) const;

    // ----- Simulation -----
    void propagate();
    void syncVisuals(); // pushes Gate logic states into GateView / Wire visuals

private:
    Circuit m_circuit;
    std::unordered_map<int, GateView> m_gateViews;
    std::vector<Wire> m_wires;
};