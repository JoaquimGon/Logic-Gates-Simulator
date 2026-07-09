#pragma once
#include <vector>
#include "gate.h"

class Circuit
{
private:
    std::vector<Gate> m_gates;



public:
    int addGate(GateType type, bool outInverted = false);

    Gate& getGate(int gateId);

    void connectGates(int srcGateId, int destGateId, int destPinIndex);

};
