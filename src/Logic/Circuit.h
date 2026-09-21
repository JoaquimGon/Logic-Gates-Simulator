#pragma once
#include "Gate.h"
#include "InputPin.h"

#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <iostream>
#include <memory>

/*
* @brief Outcome of (re)building the evaluation order.
*/
enum class EvalOrderResult { OK, CYCLE_DETECTED };

/*
* @brief Class that handles connections and components.
*/
class Circuit
{
private:
    std::unordered_map<int, std::unique_ptr<Component>> m_components;
    std::vector<int> m_evaluationOrder;
    int m_currentId{ 0 };
    bool m_evalOrderDirty{ true };

    /*
    * @brief Depth First Sort to topologically sort the component list.
    * @param componentId ID of the starting component.
    * @param visited Components already visited.
    * @param scheduled Components currently on the recursion stack (cycle detection).
    * @param order The current order (recursion).
    * @return false when a cycle was reached, in which case 'order' must be discarded.
    */
    bool dfsSort(int componentId, std::unordered_set<int>& visited, std::unordered_set<int>& scheduled, std::vector<int>& order);

    /*
    * @brief Wrapper of the DFS that starts and prepares the recursive sort.
    * @return CYCLE_DETECTED when the netlist contains a combinational loop, in which
    *         case the stored order is left untouched.
    */
    EvalOrderResult evaluateOrder();

    /*
    * @brief Whether connecting src's output to dest's input would close a loop.
    * @param srcComponentId The future driver.
    * @param destComponentId The future sink.
    * @return true when dest can already reach src, i.e. when the new edge would make
    *         the graph cyclic and a topological order impossible to build.
    */
    bool wouldCreateCycle(int srcComponentId, int destComponentId);

public:
    int addGate(GateType type);
    int addInputPin(bool initialState = false);

    Component* getComponent(int id);
    void delComponent(int id);

    bool connectComponents(int srcComponentId, int destComponentId, int destPinIndex);
    void disconnectComponents(int srcComponentId, int destComponentId, int destPinIndex);

    /*
    * @brief Settles the circuit: rebuilds the evaluation order when the netlist
    * changed, then evaluates every component in that order.
    * @return CYCLE_DETECTED when the netlist contains a combinational loop; nothing is
    *         evaluated in that case and the last known-good order is kept.
    */
    EvalOrderResult propagate();
};