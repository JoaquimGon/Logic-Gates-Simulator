#include "Circuit.h"

int Circuit::addGate(GateType type)
{
    int id = m_currentId++;
    m_components.emplace(id, std::make_unique<Gate>(id, type));
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

    for (const auto& c : comp->getOutConnections()) {
        if (Component* dest = getComponent(c.gateId)) {
            dest->delInConnection(id, c.pinIndex);

            // NEW: Pull the input low on the destination component since its power source was just deleted!
            dest->setStateInPin(c.pinIndex, false);
        }
    }

    m_components.erase(id);
    m_evalOrderDirty = true;
}

bool Circuit::connectComponents(int srcComponentId, int destComponentId, int destPinIndex)
{
    // A component feeding one of its own inputs is a cycle of length one and
    // must never enter either the netlist or the topological evaluation order.
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
            if (c.gateId == srcComponentId) {
                return true; // Already connected exactly like this, report success
            }
            std::cerr << "[Circuit] Refused connection " << srcComponentId << " -> "
                << destComponentId << " (pin " << destPinIndex
                << "): pin is already occupied.\n";
            return false;
        }
    }

    // Closing a combinational loop would make the topological order impossible to
    // build, so the edge is refused before it ever reaches the netlist. This keeps the
    // cycle handling in propagate() a defensive invariant instead of the normal path.
    // (Latches and flip-flops will need loops to be allowed through clocked components,
    // at which point this check has to ignore paths that run through a register.)
    if (wouldCreateCycle(srcComponentId, destComponentId)) {
        std::cerr << "[Circuit] Refused connection " << srcComponentId << " -> "
            << destComponentId << " (pin " << destPinIndex
            << "): it would create a combinational loop.\n";
        return false;
    }

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

    // TEST
    dest->setStateInPin(destPinIndex, false);


    m_evalOrderDirty = true;
}

bool Circuit::wouldCreateCycle(int srcComponentId, int destComponentId)
{
    // Adding src -> dest closes a loop when dest can already reach src. Only
    // out-connections are walked, which is correct while every component is
    // combinational: the netlist is a DAG.
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
                // 'order' is only partially filled, so it is discarded instead of being
                // committed: the previous known-good order stays in place. m_evalOrderDirty
                // also stays set, so the next edit to the netlist re-tries the sort.
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
    // Still being on the recursion stack means this component is reached twice along one
    // path: a combinational loop. It is reported instead of thrown, because a user can
    // reach this state by drawing wires, and an exception per frame used to leave the
    // circuit evaluating a stale order with no indication that it had given up.
    if (scheduled.find(componentId) != scheduled.end()) return false;
    if (visited.find(componentId) != visited.end()) return true;

    scheduled.insert(componentId);

    if (Component* comp = getComponent(componentId)) {   // <-- was getGate(gateId), which didn't exist
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

EvalOrderResult Circuit::propagate()
{
    if (m_evalOrderDirty) {
        EvalOrderResult result = evaluateOrder();
        if (result == EvalOrderResult::CYCLE_DETECTED) {
            // Nothing is evaluated against a stale order: the values on screen would be
            // wrong with no way to tell. The netlist stays dirty, so breaking the loop
            // (rewiring or deleting a component) rebuilds the order on the next call.
            return result;
        }
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

    return EvalOrderResult::OK;
}