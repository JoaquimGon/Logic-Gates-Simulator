
#include "circuit.h"
#include "gate.h"
#include <vector>
#include <stdexcept>
#include <iostream>
   

int Circuit::addGate(GateType type, bool outInverted)
{
    m_gates.emplace_back(type, outInverted);
    m_evalOrderDirty = true;

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
void Circuit::evaluateOrder()
{
    std::vector<bool> visited(m_gates.size(), false);
    std::vector<bool> scheduled(m_gates.size(), false);
    std::vector<int> order;

    for (int i = 0; i < m_gates.size(); ++i)
    {
        if (!visited[i]) {
            Circuit::dfsSort(i, visited, scheduled, order);
        }
    }

    m_evaluationOrder = order;
    std::reverse(m_evaluationOrder.begin(), m_evaluationOrder.end());
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
    if (m_evalOrderDirty)
    {
        evaluateOrder();
        m_evalOrderDirty = false;
        std::cout << "Order evaluated: ";
        for (int i = 0; i < m_evaluationOrder.size()-1; i++)
        {
            std::cout << "" << m_evaluationOrder[i] << "  ";
        }
        std::cout << "\n";
    }

    for (int id : m_evaluationOrder)
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
