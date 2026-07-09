
#include "circuit.h"
#include "gate.h"
#include <vector>
#include <stdexcept>

   

int Circuit::addGate(GateType type, bool outInverted)
{
    m_gates.emplace_back(type, outInverted);
    // return ID
    return m_gates.size() - 1;
}

Gate& Circuit::getGate(int gateId)
{
    return m_gates.at(gateId);
}


void Circuit::connectGates(int srcGateId, int destGateId, int destPinIndex)
{
    m_gates[srcGateId].addConnection(destGateId, destPinIndex);
}


