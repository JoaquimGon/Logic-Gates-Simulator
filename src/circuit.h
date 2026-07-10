#pragma once
#include <vector>
#include "gate.h"

class Circuit
{
private:
    std::vector<Gate> m_gates;
    std::vector<int> m_evaluationOrder;
    bool m_evalOrderDirty{ true };

    void dfsSort(int gateId, std::vector<bool>& visited, std::vector<bool>& scheduled, std::vector<int>& order);
    void evaluateOrder();

public:
    int addGate(GateType type, bool outInverted = false);
    Gate& getGate(int gateId);
    bool connectGates(int srcGateId, int destGateId, int destPinIndex);
    void propagate();

};
