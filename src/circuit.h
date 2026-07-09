#pragma once
#include <vector>
#include "gate.h"

class Circuit
{
private:
    std::vector<Gate> m_gates;

    void dfsSort(int gateId, std::vector<bool>& visited, std::vector<bool>& scheduled, std::vector<int>& order);


public:
    int addGate(GateType type, bool outInverted = false);

    Gate& getGate(int gateId);

    void connectGates(int srcGateId, int destGateId, int destPinIndex);

    std::vector<int> getEvaluationOrder();

    void propagate();
};
