#include "Circuit.h"

int Circuit::addGate(GateType type)
{
    int id = m_currentId++;
    m_components.emplace(id, std::make_unique<Gate>(id, type));
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
    if (!comp) return;

    for (const auto& c : comp->getInConnections())
        if (Component* src = getComponent(c.gateId)) src->delOutConnection(id, c.pinIndex);

    for (const auto& c : comp->getOutConnections()) {
        if (Component* dest = getComponent(c.gateId)) {
            dest->delInConnection(id, c.pinIndex);
            dest->setStateInPin(c.pinIndex, false);
        }
    }

    m_components.erase(id);
    m_evalOrderDirty = true;
    m_stateDirty = true;
}

bool Circuit::connectComponents(int srcComponentId, int destComponentId, int destPinIndex)
{
    if (srcComponentId == destComponentId) return false;

    Component* src = getComponent(srcComponentId);
    Component* dest = getComponent(destComponentId);
    if (!src || !dest) return false;

    if (src->getOutputPinCount() <= 0 ||
        destPinIndex < 0 || destPinIndex >= dest->getInputPinCount()) {
        return false;
    }

    for (const auto& c : dest->getInConnections()) {
        if (c.pinIndex == destPinIndex) {
            if (c.gateId == srcComponentId) return true;
            return false;
        }
    }

    if (wouldCreateCycle(srcComponentId, destComponentId)) {
        return false;
    }

    src->addOutConnection(destComponentId, destPinIndex);
    dest->addInConnection(srcComponentId, destPinIndex);
    m_evalOrderDirty = true;
    m_stateDirty = true;
    return true;
}

void Circuit::disconnectComponents(int srcComponentId, int destComponentId, int destPinIndex)
{
    Component* src = getComponent(srcComponentId);
    Component* dest = getComponent(destComponentId);
    if (!src || !dest) return;

    src->delOutConnection(destComponentId, destPinIndex);
    dest->delInConnection(srcComponentId, destPinIndex);
    dest->setStateInPin(destPinIndex, false);

    m_evalOrderDirty = true;
    m_stateDirty = true;
}

void Circuit::clearConnections()
{
    for (auto& [id, component] : m_components) {
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
    std::vector<int> stack{ destComponentId };

    while (!stack.empty()) {
        int currentId = stack.back();
        stack.pop_back();

        if (currentId == srcComponentId) return true;
        if (!visited.insert(currentId).second) continue;

        if (Component* comp = getComponent(currentId))
            for (const auto& conn : comp->getOutConnections())
                stack.push_back(conn.gateId);
    }

    return false;
}

EvalOrderResult Circuit::evaluateOrder()
{
    std::unordered_set<int> visited;
    std::unordered_set<int> scheduled;
    std::vector<int> order;

    for (const auto& pair : m_components) {
        int currentId = pair.first;
        if (visited.find(currentId) == visited.end()) {
            if (!dfsSort(currentId, visited, scheduled, order)) {
                return EvalOrderResult::CYCLE_DETECTED;
            }
        }
    }

    m_evaluationOrder = std::move(order);
    std::reverse(m_evaluationOrder.begin(), m_evaluationOrder.end());
    return EvalOrderResult::OK;
}

bool Circuit::dfsSort(int componentId, std::unordered_set<int>& visited, std::unordered_set<int>& scheduled, std::vector<int>& order)
{
    if (scheduled.find(componentId) != scheduled.end()) return false;
    if (visited.find(componentId) != visited.end()) return true;

    scheduled.insert(componentId);

    if (Component* comp = getComponent(componentId)) {
        for (const auto& conn : comp->getOutConnections()) {
            if (!dfsSort(conn.gateId, visited, scheduled, order)) {
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
    for (auto& [id, comp] : m_components) {
        if (auto* clk = dynamic_cast<Clock*>(comp.get())) {
            if (clk->advanceTime(deltaTime)) {
                clockEdgeOccurred = true;
            }
        }
    }

    if (clockEdgeOccurred) {
        m_stateDirty = true;
    }
    return clockEdgeOccurred;
}

EvalOrderResult Circuit::propagate()
{
    // Step 1: Structural sort only when topology changed
    if (m_evalOrderDirty) {
        EvalOrderResult result = evaluateOrder();
        if (result == EvalOrderResult::CYCLE_DETECTED) {
            return result;
        }
        m_evalOrderDirty = false;
    }

    // Step 2: Forward evaluation pass
    for (int id : m_evaluationOrder) {
        Component* comp = getComponent(id);
        if (!comp) continue;

        comp->evaluate();
        bool out = comp->getStateOutPin();

        for (const auto& connection : comp->getOutConnections()) {
            if (Component* child = getComponent(connection.gateId)) {
                child->setStateInPin(connection.pinIndex, out);
            }
        }
    }

    m_stateDirty = false;
    return EvalOrderResult::OK;
}