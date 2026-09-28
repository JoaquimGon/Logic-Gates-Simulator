#pragma once
#include "Clock.h"
#include "Gate.h"
#include "InputPin.h"

#include <iostream>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

enum class EvalOrderResult
{
    OK,
    CYCLE_DETECTED
};

class Circuit
{
  private:
    std::unordered_map<int, std::unique_ptr<Component>> m_components;
    std::vector<int> m_evaluationOrder;
    int m_currentId{0};
    bool m_evalOrderDirty{true};
    bool m_stateDirty{true};

    bool dfsSort(
        int componentId,
        std::unordered_set<int>& visited,
        std::unordered_set<int>& scheduled,
        std::vector<int>& order
    );
    EvalOrderResult evaluateOrder();
    bool wouldCreateCycle(int srcComponentId, int destComponentId);

  public:
    int addGate(GateType type);
    int addInputPin(bool initialState = false);
    int addClock(float frequencyHz = 1.0f); // NEW

    Component* getComponent(int id);
    void delComponent(int id);

    bool connectComponents(int srcComponentId, int destComponentId, int destPinIndex);
    void disconnectComponents(int srcComponentId, int destComponentId, int destPinIndex);
    void clearConnections();

    void markStateDirty() { m_stateDirty = true; }

    bool isStateDirty() const { return m_stateDirty || m_evalOrderDirty; }

    /**
     * @brief Advances all clocks in the circuit.
     * @return true if any clock completed a cycle and flipped state.
     */
    bool updateClocks(float deltaTime);

    EvalOrderResult propagate();
};