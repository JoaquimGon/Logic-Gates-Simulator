#include "Circuit.h"

#include <algorithm>
#include <chrono>
#include <stdexcept>
#include <utility>

Circuit::Circuit(const Circuit& other)
    : m_evaluationOrder(other.m_evaluationOrder), m_currentId(other.m_currentId),
      m_evalOrderDirty(other.m_evalOrderDirty), m_stateDirty(other.m_stateDirty),
      m_lastPropagateDurationMs(other.m_lastPropagateDurationMs),
      m_lastEvalResult(other.m_lastEvalResult)
{
    for (const auto& [id, component] : other.m_components)
        m_components.emplace(id, component->clone());
}

Circuit& Circuit::operator=(const Circuit& other)
{
    if (this != &other)
    {
        Circuit copy(other);
        *this = std::move(copy);
    }
    return *this;
}

void Circuit::preserveAllocatedIds(const Circuit& other)
{
    m_currentId = std::max(m_currentId, other.m_currentId);
}

void Circuit::resizeGateInputs(int componentId, int inputPinCount)
{
    auto* gate = dynamic_cast<Gate*>(getComponent(componentId));
    if (!gate || !Gate::isValidInputPinCount(gate->getType(), inputPinCount))
        throw std::invalid_argument("Invalid native gate input count.");
    const auto connections = gate->getInConnections();
    for (const auto& edge : connections)
        if (edge.destPinIndex >= inputPinCount)
            disconnectComponents(
                edge.srcComponentId, edge.srcPinIndex, componentId, edge.destPinIndex
            );
    gate->m_stateInPins.resize(inputPinCount, false);
    m_evalOrderDirty = true;
    m_stateDirty = true;
}

int Circuit::addGate(GateType type)
{
    return addGate(type, type == NOT ? 1 : 2);
}

int Circuit::addGate(GateType type, int inputPinCount)
{
    int id = m_currentId++;
    m_components.emplace(id, std::make_unique<Gate>(id, type, inputPinCount));
    m_evalOrderDirty = true;
    m_stateDirty = true;
    return id;
}

int Circuit::addInputPin(bool initialState)
{
    int id = m_currentId++;
    m_components.emplace(id, std::make_unique<InputPin>(id, initialState));
    m_evalOrderDirty = true;
    m_stateDirty = true;
    return id;
}

Component* Circuit::getComponent(int id)
{
    auto it = m_components.find(id);
    return it != m_components.end() ? it->second.get() : nullptr;
}

void Circuit::delComponent(int id)
{
    Component* comp = getComponent(id);
    if (!comp)
        return;

    for (const auto& connection : comp->getInConnections())
        if (Component* src = getComponent(connection.srcComponentId))
            src->delOutConnection(connection);

    for (const auto& connection : comp->getOutConnections())
    {
        if (Component* dest = getComponent(connection.destComponentId))
        {
            dest->delInConnection(connection);
            dest->setStateInPin(connection.destPinIndex, false);
        }
    }

    m_components.erase(id);
    m_evalOrderDirty = true;
    m_stateDirty = true;
}

bool Circuit::connectComponents(
    int srcComponentId, int srcPinIndex, int destComponentId, int destPinIndex
)
{
    return tryConnectComponents(srcComponentId, srcPinIndex, destComponentId, destPinIndex) ==
           ConnectionResult::OK;
}

ConnectionResult Circuit::tryConnectComponents(
    int srcComponentId, int srcPinIndex, int destComponentId, int destPinIndex
)
{
    Component* src = getComponent(srcComponentId);
    Component* dest = getComponent(destComponentId);
    if (!src || !dest)
        return ConnectionResult::INVALID_COMPONENT;

    if (srcPinIndex < 0 || srcPinIndex >= src->getOutputPinCount() || destPinIndex < 0 ||
        destPinIndex >= dest->getInputPinCount())
    {
        return ConnectionResult::INVALID_PIN;
    }

    if (srcComponentId == destComponentId)
        return ConnectionResult::CYCLE_DETECTED;

    const Connection connection{srcComponentId, srcPinIndex, destComponentId, destPinIndex};
    for (const auto& existing : dest->getInConnections())
    {
        if (existing.destPinIndex == destPinIndex)
            return existing == connection ? ConnectionResult::OK
                                          : ConnectionResult::INPUT_ALREADY_DRIVEN;
    }

    if (wouldCreateCycle(srcComponentId, destComponentId))
        return ConnectionResult::CYCLE_DETECTED;

    src->addOutConnection(connection);
    dest->addInConnection(connection);
    m_evalOrderDirty = true;
    m_stateDirty = true;
    return ConnectionResult::OK;
}

void Circuit::disconnectComponents(
    int srcComponentId, int srcPinIndex, int destComponentId, int destPinIndex
)
{
    Component* src = getComponent(srcComponentId);
    Component* dest = getComponent(destComponentId);
    if (!src || !dest)
        return;

    const Connection connection{srcComponentId, srcPinIndex, destComponentId, destPinIndex};
    const auto& outgoing = src->getOutConnections();
    if (std::find(outgoing.begin(), outgoing.end(), connection) == outgoing.end())
        return;

    src->delOutConnection(connection);
    dest->delInConnection(connection);
    dest->setStateInPin(destPinIndex, false);

    m_evalOrderDirty = true;
    m_stateDirty = true;
}

void Circuit::clearConnections()
{
    for (auto& [id, component] : m_components)
    {
        component->clearConnections();
        for (int pin = 0; pin < component->getInputPinCount(); ++pin)
            component->setStateInPin(pin, false);
    }

    m_evalOrderDirty = true;
    m_stateDirty = true;
}

bool Circuit::wouldCreateCycle(int srcComponentId, int destComponentId)
{
    std::unordered_set<int> visited;
    std::vector<int> stack{destComponentId};

    while (!stack.empty())
    {
        int currentId = stack.back();
        stack.pop_back();

        if (currentId == srcComponentId)
            return true;
        if (!visited.insert(currentId).second)
            continue;

        if (Component* comp = getComponent(currentId))
            for (const auto& conn : comp->getOutConnections())
                stack.push_back(conn.destComponentId);
    }

    return false;
}

EvalOrderResult Circuit::evaluateOrder()
{
    std::unordered_set<int> visited;
    std::unordered_set<int> scheduled;
    std::vector<int> order;

    for (const auto& pair : m_components)
    {
        int currentId = pair.first;
        if (visited.find(currentId) == visited.end())
        {
            if (!dfsSort(currentId, visited, scheduled, order))
            {
                return EvalOrderResult::CYCLE_DETECTED;
            }
        }
    }

    m_evaluationOrder = std::move(order);
    std::reverse(m_evaluationOrder.begin(), m_evaluationOrder.end());
    return EvalOrderResult::OK;
}

bool Circuit::dfsSort(
    int componentId,
    std::unordered_set<int>& visited,
    std::unordered_set<int>& scheduled,
    std::vector<int>& order
)
{
    if (scheduled.find(componentId) != scheduled.end())
        return false;
    if (visited.find(componentId) != visited.end())
        return true;

    scheduled.insert(componentId);

    if (Component* comp = getComponent(componentId))
    {
        for (const auto& conn : comp->getOutConnections())
        {
            if (!dfsSort(conn.destComponentId, visited, scheduled, order))
            {
                scheduled.erase(componentId);
                return false;
            }
        }
    }

    scheduled.erase(componentId);
    visited.insert(componentId);
    order.push_back(componentId);
    return true;
}

int Circuit::addClock(float frequencyHz)
{
    int id = m_currentId++;
    m_components.emplace(id, std::make_unique<Clock>(id, frequencyHz));
    m_evalOrderDirty = true;
    m_stateDirty = true;
    return id;
}

bool Circuit::updateClocks(float deltaTime)
{
    bool clockEdgeOccurred = false;
    for (auto& [id, comp] : m_components)
    {
        if (auto* clk = dynamic_cast<Clock*>(comp.get()))
        {
            if (clk->advanceTime(deltaTime))
            {
                clockEdgeOccurred = true;
            }
        }
    }

    if (clockEdgeOccurred)
    {
        m_stateDirty = true;
    }
    return clockEdgeOccurred;
}


EvalOrderResult Circuit::propagate()
{
    auto startTime = std::chrono::high_resolution_clock::now();

    // Step 1: Structural sort only when topology changed
    if (m_evalOrderDirty)
    {
        EvalOrderResult result = evaluateOrder();
        m_lastEvalResult = result;
        if (result == EvalOrderResult::CYCLE_DETECTED)
        {
            auto endTime = std::chrono::high_resolution_clock::now();
            m_lastPropagateDurationMs =
                std::chrono::duration<float, std::milli>(endTime - startTime).count();
            return result;
        }
        m_evalOrderDirty = false;
    }

    // Step 2: Forward evaluation pass
    for (int id : m_evaluationOrder)
    {
        Component* comp = getComponent(id);
        if (!comp)
            continue;

        comp->evaluate();

        for (const auto& connection : comp->getOutConnections())
        {
            if (Component* child = getComponent(connection.destComponentId))
            {
                child->setStateInPin(
                    connection.destPinIndex, comp->getStateOutPin(connection.srcPinIndex)
                );
            }
        }
    }

    auto endTime = std::chrono::high_resolution_clock::now();
    m_lastPropagateDurationMs =
        std::chrono::duration<float, std::milli>(endTime - startTime).count();

    m_stateDirty = false;
    return EvalOrderResult::OK;
}

int Circuit::addLatch(LatchType type)
{
    int id = m_currentId++;
    m_components.emplace(id, std::make_unique<Latch>(id, type));
    m_evalOrderDirty = true;
    m_stateDirty = true;
    return id;
}