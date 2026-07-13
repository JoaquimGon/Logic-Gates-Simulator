
#include "circuit.h"
#include "gate.h"
#include <vector>
#include <unordered_set>
#include <stdexcept>
#include <iostream>


int Circuit::addGate(GateType type, bool outInverted)
{
    m_gates.emplace(m_currentId, Gate(m_currentId, type, outInverted));
    m_currentId++;
    m_evalOrderDirty = true;
    // return ID
    return m_currentId - 1;
}

Gate* Circuit::getGate(int gateId)
{
    auto it = m_gates.find(gateId);

    if (it != m_gates.end()) {
        return &(it->second);
    }

    return nullptr; // Not found
}

// Adds connection to in and out of each gate
bool Circuit::connectGates(int srcGateId, int destGateId, int destPinIndex)
{
    Gate* destGate = getGate(destGateId);
    Gate* srcGate = getGate(srcGateId);

    for (const Connection& connection : destGate->getInConnections())
    {
        if (connection.pinIndex == destPinIndex)
        {
            return false;
        }
       
    }
    
    srcGate->addOutConnection(destGateId, destPinIndex);
    destGate->addInConnection(srcGateId, destPinIndex);
    return true;
}

// Topological sort
void Circuit::evaluateOrder()
{
    // Tracks state by Unique ID instead of array index position
    std::unordered_set<int> visited;
    std::unordered_set<int> scheduled;
    std::vector<int> order;

    for (const auto& pair : m_gates)
    {
        int currentGateId = pair.first;

        if (visited.find(currentGateId) == visited.end()) {
            Circuit::dfsSort(currentGateId, visited, scheduled, order);
        }
    }

    m_evaluationOrder = order;
    std::reverse(m_evaluationOrder.begin(), m_evaluationOrder.end());
}


// Depth first sorting for a topological sort
void Circuit::dfsSort(int gateId, std::unordered_set<int>& visited, std::unordered_set<int>& scheduled, std::vector<int>& order)
{
    if (scheduled.find(gateId) != scheduled.end()) {
        throw std::runtime_error("Cyclic dependency detected! (Latches require a clock-staged evaluation).");
    }

    if (visited.find(gateId) != visited.end()) return;
    scheduled.insert(gateId);

    Gate* currentGate = getGate(gateId);
    if (currentGate != nullptr)
    {
        for (const auto& conn : currentGate->getOutConnections())
        {
            dfsSort(conn.gateId, visited, scheduled, order);
        }
    }

    scheduled.erase(gateId);
    visited.insert(gateId);
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

        Gate* gate = getGate(id);
        gate->evaluateOut();


        // Update the child gates
        bool currentOutput = gate->getStateOutPin();
        std::vector<Connection> connections{ gate->getOutConnections() };

        for (const auto& connection : connections)
        {
            getGate(connection.gateId)->setStateInPins(connection.pinIndex, currentOutput);
        }

    }
}
