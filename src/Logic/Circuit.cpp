#include "Circuit.h"

int Circuit::addGate(GateType type, bool outInverted)
{
    int id = m_currentId++;
    m_components.emplace(id, std::make_unique<Gate>(id, type, outInverted));
    m_evalOrderDirty = true;
    return id;
}

int Circuit::addInputPin(bool initialState)
{
    int id = m_currentId++;
    m_components.emplace(id, std::make_unique<InputPin>(id, initialState));
    m_evalOrderDirty = true;
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

    for (const auto& c : comp->getOutConnections())
        if (Component* dest = getComponent(c.gateId)) dest->delInConnection(id, c.pinIndex);

    m_components.erase(id);
    m_evalOrderDirty = true;
}

bool Circuit::connectComponents(int srcComponentId, int destComponentId, int destPinIndex)
{
    Component* src = getComponent(srcComponentId);
    Component* dest = getComponent(destComponentId);
    if (!src || !dest) return false;

    for (const auto& c : dest->getInConnections())
        if (c.pinIndex == destPinIndex) return false;

    src->addOutConnection(destComponentId, destPinIndex);
    dest->addInConnection(srcComponentId, destPinIndex);
    m_evalOrderDirty = true;
    return true;
}

void Circuit::disconnectComponents(int srcComponentId, int destComponentId, int destPinIndex)
{
    Component* src = getComponent(srcComponentId);
    Component* dest = getComponent(destComponentId);
    if (!src || !dest) return;

    src->delOutConnection(destComponentId, destPinIndex);
    dest->delInConnection(srcComponentId, destPinIndex);
    m_evalOrderDirty = true;
}

void Circuit::changeConnection(
    int srcComponentId,
    int oldDestComponentId, int oldDestPinIndex,
    int newDestComponentId, int newDestPinIndex)
{
    disconnectComponents(srcComponentId, oldDestComponentId, oldDestPinIndex);
    connectComponents(srcComponentId, newDestComponentId, newDestPinIndex);
}

void Circuit::evaluateOrder()
{
    std::unordered_set<int> visited;
    std::unordered_set<int> scheduled;
    std::vector<int> order;

    for (const auto& pair : m_components) {
        int currentId = pair.first;
        if (visited.find(currentId) == visited.end()) {
            dfsSort(currentId, visited, scheduled, order);
        }
    }

    m_evaluationOrder = order;
    std::reverse(m_evaluationOrder.begin(), m_evaluationOrder.end());
}

void Circuit::dfsSort(int componentId, std::unordered_set<int>& visited, std::unordered_set<int>& scheduled, std::vector<int>& order)
{
    if (scheduled.find(componentId) != scheduled.end()) {
        throw std::runtime_error("Cyclic dependency detected! (Latches require a clock-staged evaluation).");
    }
    if (visited.find(componentId) != visited.end()) return;

    scheduled.insert(componentId);

    if (Component* comp = getComponent(componentId)) {   // <-- was getGate(gateId), which didn't exist
        for (const auto& conn : comp->getOutConnections()) {
            dfsSort(conn.gateId, visited, scheduled, order);
        }
    }

    scheduled.erase(componentId);
    visited.insert(componentId);
    order.push_back(componentId);
}

void Circuit::propagate()
{
    if (m_evalOrderDirty) {
        evaluateOrder();
        m_evalOrderDirty = false;
    }

    for (int id : m_evaluationOrder) {
        Component* comp = getComponent(id);
        if (!comp) continue;

        comp->evaluate();
        bool out = comp->getStateOutPin();

        for (const auto& connection : comp->getOutConnections())
            if (Component* child = getComponent(connection.gateId))
                child->setStateInPin(connection.pinIndex, out);
    }
}