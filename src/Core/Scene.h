#pragma once
#include <vector>
#include <unordered_map>
#include <glm/glm.hpp>
#include "..\Views\GateView.h"
#include "..\Logic\Wire.h"
#include "..\Views\GridSystem.h"
#include "..\Views\InputPinView.h"
#include "../Logic/Circuit.h"

enum class HitType { NONE, COMPONENT_PIN, COMPONENT_BODY, WIRE_START, WIRE_END, WIRE_BODY };

struct HitResult {
    HitType type = HitType::NONE;
    int componentId = -1;              // valid for COMPONENT_PIN, COMPONENT_BODY
    int pinIndex = -1;                 // valid for COMPONENT_PIN
    PinType pinType = PinType::INPUT;  // valid for COMPONENT_PIN
    int wireIndex = -1;                // valid for WIRE_*
};

class Scene
{
public:
    // ----- Gates (the only concrete component type today) -----
    int addGate(GateType type, GridCoords gridPos, glm::vec2 size, const std::string& shaderName,
        std::vector<PinUI> inputs, std::vector<PinUI> outputs, bool outInverted = false);

    int addInputPin(GridCoords gridPos, glm::vec2 size, const std::string& shaderName, bool initialState = false);


    void removeComponent(int componentId);
    ComponentView* getComponentView(int componentId);
    Component* getLogicComponent(int componentId);
    const std::unordered_map<int, std::unique_ptr<ComponentView>>& getComponentViewMap() const { return m_componentViews; }

    // ----- Wires (Input never touches this vector directly) -----
    size_t wireCount() const { return m_wires.size(); }
    Wire& wireAt(size_t index) { return m_wires[index]; }
    const std::vector<Wire>& getWires() const { return m_wires; }

    size_t commitWire(Wire wire);
    Wire   extractWire(size_t index);
    bool   splitWireAt(size_t index, GridCoords point, Wire& outA, Wire& outB);
    void   addWires(Wire a, Wire b);
    void   removeWire(size_t index);

    // Reattaches any dangling wire endpoints at this component's pins after a drag.
    void reconnectWiresToComponent(int componentId);

    // ----- Logic connections -----
    bool connectPins(int srcComponentId, int destComponentId, int destPinIndex);
    void disconnectPins(int srcComponentId, int destComponentId, int destPinIndex);

    // ----- Hit-testing -----
    HitResult hitTest(glm::vec2 worldPos, GridCoords gridPos) const;

    // ----- Simulation -----
    void propagate();
    void syncVisuals();
    bool handleClick(int componentId);
private:
    Circuit m_circuit;
    std::unordered_map<int, std::unique_ptr<ComponentView>> m_componentViews;
    std::vector<Wire> m_wires;
};