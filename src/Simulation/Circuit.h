#pragma once
#include "Components/Clock.h"
#include "Components/Gate.h"
#include "Components/InputPin.h"
#include "Components/Latch.h"
#include "Components/OutputPin.h"
#include "Simulation/SimulationStatus.h"

#include <cstddef>
#include <memory>
#include <unordered_map>
#include <vector>

enum class ConnectionResult
{
    OK,
    INVALID_COMPONENT,
    INVALID_PIN,
    INPUT_ALREADY_DRIVEN
};

class Circuit
{
  private:
    std::unordered_map<int, std::unique_ptr<Component>> m_components;
    std::vector<int> m_componentOrder;
    std::unordered_map<int, std::vector<bool>> m_outputStates;
    int m_currentId{0};
    bool m_structureDirty{true};
    bool m_stateDirty{true};
    double m_pendingClockTime = 0;

    void publishOutputs(const std::vector<int>& components, std::vector<int>& changed);

    float m_lastPropagateDurationMs = 0.0f;
    SimulationResult m_lastEvalResult = SimulationResult::OK;

  public:
    Circuit() = default;
    Circuit(const Circuit& other);
    Circuit& operator=(const Circuit& other);
    Circuit(Circuit&&) noexcept = default;
    Circuit& operator=(Circuit&&) noexcept = default;

    /** @brief Resizes a native gate, retaining surviving indexed states and pruning removed edges.
     */
    void resizeGateInputs(int componentId, int inputPinCount);
    /** @brief Prevents snapshot restoration from recycling previously allocated component IDs. */
    void preserveAllocatedIds(const Circuit& other);

    int addGate(GateType type);
    int addGate(GateType type, int inputPinCount);
    int addInputPin(bool initialState = false);
    int addOutputPin();
    int addClock(float frequencyHz = 1.0f);
    int addLatch(LatchType type);
    /** @brief Takes ownership of a logical component and assigns its circuit-local ID. */
    int addComponent(std::unique_ptr<Component> component);

    Component* getComponent(int id);
    const Component* getComponent(int id) const;
    void delComponent(int id);

    /**
     * @brief Connects an output to an input, preserving both endpoint indices.
     * @param srcComponentId Source component.
     * @param srcPinIndex Source output index.
     * @param destComponentId Destination component.
     * @param destPinIndex Destination input index.
     * @return True for a new or identical edge, including feedback; false for invalid endpoints
     * or an occupied input.
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

    bool isStateDirty() const { return m_stateDirty || m_structureDirty; }

    /**
     * @brief Advances clocks chronologically, settling the circuit after each transition time.
     * Work is bounded per call; unprocessed elapsed time is retained for the next call.
     * @return true if at least one clock edge was processed.
     */
    bool updateClocks(float deltaTime);

    SimulationResult propagate();

    // Debugging:
    float getLastPropagateTimeMs() const { return m_lastPropagateDurationMs; }

    SimulationResult getLastEvalResult() const { return m_lastEvalResult; }

    size_t getSimulationComponentCount() const { return m_componentOrder.size(); }

    size_t getComponentCount() const { return m_components.size(); }
};
