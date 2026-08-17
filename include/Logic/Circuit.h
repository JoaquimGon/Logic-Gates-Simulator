#pragma once
#include "Gate.h"

#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <stdexcept>
#include <iostream>

/*
* @brief Class that handles connections and components.
*/
class Circuit
{
private:
    std::unordered_map<int, Gate>m_gates;
    std::vector<int> m_evaluationOrder;
    int m_currentId{ 0 };
    bool m_evalOrderDirty{ true };

    /*
    * @brief Depth First Sort to topologically sort the gate list.
    * @param gateId ID of the starting gate.
    * @param visited List of the already visited gates.
    * @param scheduled List of the qeued gates seen but not visited.
    * @param order The current order (recursion).
    */
    void dfsSort(int gateId, std::unordered_set<int>& visited, std::unordered_set<int>& scheduled, std::vector<int>& order);

    /*
    * @brief Wrapper of the DFS that starts and prepares the recursive sort.
    */
    void evaluateOrder();

public:
    /*
    * @brief Adds a gate.
    * @param Type of the gate to be added.
    * @param outInverted If the output is inverted.
    */
    int addGate(GateType type, bool outInverted = false);

    /*
    * @brief Safely gets a gate from the gate list.
    * @param gateId ID of the gate.
    */
    Gate* getGate(int gateId);

    /*
    * @brief Deletes a gate and it's output and input connections.
    * @param gateId ID of the gate.
    */
    void delGate(int gateId);

    /*
    * @brief Connects 2 gates (output of source gate -> destination gate).
    * @param srcGateId ID of the source gate.
    * @param destGateId ID of the destinaiton gate.
    * @param destPinIndex Index of the destination gate input's pin. 
    */
    bool connectGates(int srcGateId, int destGateId, int destPinIndex);

    /*
    * @brief Deletes connection between 2 gates.
    * @param srcGateId ID of the source gate.
    * @param destGateId ID of the destinaiton gate.
    * @param destPinIndex Index of the destination gate input's pin.
    */
    void disconnectGates(int srcGateId, int destGateId, int destPinIndex);

    /*
    * @brief Changes an existing connection.
    * @param srcGateId ID of the source gate.
    * @param oldDestGateID ID of the OLD destination gate.
    * @param oldDestGatePinIndex Input pin index of the OLD destination gate.
    * @param newDestGateId ID of the NEW destination gate.
    * @param newDestGatePinIndex Input pin index of the NEW destination gate.
    */
    void changeConnection(int srcGateId,
        int oldDestGateId, int oldDestGatePinIndex, 
        int newDestGateId, int newDestGatePinIndex);

    /*
    * @brief Propagates signals using directional connections in a topologically sorted gate list
    */
    void propagate();
};
