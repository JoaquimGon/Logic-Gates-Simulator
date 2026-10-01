#pragma once
#include "Clock.h"
#include "Gate.h"
#include "InputPin.h"
#include "Latch.h"

#include <iostream>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

enum class EvalOrderResult
{
    OK,
    CYCLE_DETECTED,
    CONNECTION_REJECTED
};

enum class ConnectionResult
{
    OK,
    INVALID_COMPONENT,
    INVALID_PIN,
    INPUT_ALREADY_DRIVEN,
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

    float m_lastPropagateDurationMs = 0.0f;
    EvalOrderResult m_lastEvalResult = EvalOrderResult::OK;

  public:
    int addGate(GateType type);
    int addGate(GateType type, int inputPinCount);
    int addInputPin(bool initialState = false);
    int addClock(float frequencyHz = 1.0f); // NEW
    int addLatch(LatchType type);

    Component* getComponent(int id);
    void delComponent(int id);

    /**
     * @brief Connects an output to an input, preserving both endpoint indices.
     * @param srcComponentId Source component.
     * @param srcPinIndex Source output index.
     * @param destComponentId Destination component.
     * @param destPinIndex Destination input index.
     * @return True for a new or identical edge; false for invalid pins, an occupied input, or a
     * cycle.
     */
    bool
    connectComponents(int srcComponentId, int srcPinIndex, int destComponentId, int destPinIndex);
    /**
     * @brief Attempts a connection and reports why it cannot be added.
     * @param srcComponentId Source component.
     * @param srcPinIndex Source output index.
     * @param destComponentId Destination component.
     * @param destPinIndex Destination input index.
     * @return OK for a new or identical edge; otherwise the rejection reason.
     */
    ConnectionResult tryConnectComponents(
        int srcComponentId, int srcPinIndex, int destComponentId, int destPinIndex
    );
    void disconnectComponents(
        int srcComponentId, int srcPinIndex, int destComponentId, int destPinIndex
    );
    void clearConnections();

    void markStateDirty() { m_stateDirty = true; }

    bool isStateDirty() const { return m_stateDirty || m_evalOrderDirty; }

    /**
     * @brief Advances all clocks in the circuit.
     * @return true if any clock completed a cycle and flipped state.
     */
    bool updateClocks(float deltaTime);

    EvalOrderResult propagate();

    // Debugging:
    float getLastPropagateTimeMs() const { return m_lastPropagateDurationMs; }

    EvalOrderResult getLastEvalResult() const { return m_lastEvalResult; }

    size_t getEvalOrderSize() const { return m_evaluationOrder.size(); }

    size_t getComponentCount() const { return m_components.size(); }
};