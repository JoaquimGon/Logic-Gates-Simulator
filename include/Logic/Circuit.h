#pragma once
#include "Gate.h"
#include "InputPin.h"

#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <stdexcept>
#include <iostream>
#include <memory>

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
    */
    void dfsSort(int componentId, std::unordered_set<int>& visited, std::unordered_set<int>& scheduled, std::vector<int>& order);

    /*
    * @brief Wrapper of the DFS that starts and prepares the recursive sort.
    */
    void evaluateOrder();

public:
    int addGate(GateType type, bool outInverted = false);
    int addInputPin(bool initialState = false);

    Component* getComponent(int id);
    void delComponent(int id);

    bool connectComponents(int srcComponentId, int destComponentId, int destPinIndex);
    void disconnectComponents(int srcComponentId, int destComponentId, int destPinIndex);

    void changeConnection(int srcComponentId,
        int oldDestComponentId, int oldDestPinIndex,
        int newDestComponentId, int newDestPinIndex);

    void propagate();
};