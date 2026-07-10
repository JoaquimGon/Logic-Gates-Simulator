
#include "circuit.h"
#include "gate.h"
#include <vector>
#include <stdexcept>
#include <iostream>
   

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

// Adds connection to in and out of each gate
bool Circuit::connectGates(int srcGateId, int destGateId, int destPinIndex)
{
    for (const Connection& connection : m_gates[destGateId].getInConnections())
    {
        if (connection.pinIndex == destPinIndex)
        {
            return false;
        }
       
    }
    
    m_gates[srcGateId].addOutConnection(destGateId, destPinIndex);
    m_gates[destGateId].addInConnection(srcGateId, destPinIndex);
    return true;
}

// Topological sort
std::vector<int> Circuit::getEvaluationOrder()
{
    std::vector<bool> visited(m_gates.size(), false);
    std::vector<bool> scheduled(m_gates.size(), false);
    std::vector<int> order;

    for (size_t i = 0; i < m_gates.size(); ++i)
    {
        if (!visited[i]) {
            Circuit::dfsSort(i, visited, scheduled, order);
        }
    }

    std::reverse(order.begin(), order.end());
    return order;
}


// Depth first sorting for a topological sort
void Circuit::dfsSort(int gateId, std::vector<bool>& visited, std::vector<bool>& scheduled, std::vector<int>& order)
{
    if (scheduled[gateId]) {
        throw std::runtime_error("Cyclic dependency detected! (Latches require a clock-staged evaluation).");
    }

    if (visited[gateId]) return;

    scheduled[gateId] = true;

    for (const auto& conn : m_gates[gateId].getOutConnections())
    {
        dfsSort(conn.gateId, visited, scheduled, order);
    }

    scheduled[gateId] = false;
    visited[gateId] = true;

    order.push_back(gateId);
}

// Propagation, by iterating the sorted list
void Circuit::propagate()
{
    std::vector<int> order = getEvaluationOrder();
    for (int id : order)
    {

        m_gates[id].evaluateOut();


        // Update the child gates
        bool currentOutput = m_gates[id].getStateOutPin();
        std::vector<Connection> connections{ m_gates[id].getOutConnections() };

        for (const auto& connection : connections)
        {
            m_gates[connection.gateId].setStateInPins(connection.pinIndex, currentOutput);
        }

    }
}
