#pragma once
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include "gate.h"

class Circuit
{
private:
    std::unordered_map<int, Gate>m_gates;
    std::vector<int> m_evaluationOrder;
    int m_currentId{ 0 };
    bool m_evalOrderDirty{ true };

    void dfsSort(int gateId, std::unordered_set<int>& visited, std::unordered_set<int>& scheduled, std::vector<int>& order);
    void evaluateOrder();

public:
    int addGate(GateType type, bool outInverted = false);
    Gate* getGate(int gateId);
    void delGate(int gateId);
    bool connectGates(int srcGateId, int destGateId, int destPinIndex);
    void disconnectGates(int srcGateId, int destGateId, int destGatePinIndex);
    void changeConnection(int srcGateId,
        int oldDestGateId, int oldDestGatePinIndex, 
        int newDestGateId, int newDestGatePinIndex);
    void propagate();
};
