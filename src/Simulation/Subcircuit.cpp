#include "Simulation/Subcircuit.h"

namespace
{
const Circuit& checkedTemplate(const SubcircuitBehavior& design)
{
    if (!design.circuit)
        throw std::invalid_argument("Subcircuit simulation template is missing.");
    for (int id : design.inputs)
        if (!dynamic_cast<const InputPin*>(design.circuit->getComponent(id)))
            throw std::invalid_argument("Subcircuit input mapping must reference an input pin.");
    for (int id : design.outputs)
        if (!dynamic_cast<const OutputPin*>(design.circuit->getComponent(id)))
            throw std::invalid_argument("Subcircuit output mapping must reference an output pin.");
    return *design.circuit;
}
} // namespace

Subcircuit::Subcircuit(const SubcircuitBehavior& design)
    : Component(
          -1, static_cast<int>(design.inputs.size()), static_cast<int>(design.outputs.size())
      ),
      m_circuit(checkedTemplate(design)), m_inputs(design.inputs), m_outputs(design.outputs)
{
}

std::unique_ptr<Component> Subcircuit::clone() const
{
    return std::make_unique<Subcircuit>(*this);
}

void Subcircuit::evaluate()
{
    std::size_t remaining = 100000;
    evaluate(remaining);
}

void Subcircuit::evaluate(std::size_t& remainingEvaluations)
{
    for (std::size_t i = 0; i < m_inputs.size(); ++i)
        static_cast<InputPin*>(m_circuit.getComponent(m_inputs[i]))
            ->setState(getStateInPin(static_cast<int>(i)));
    m_circuit.propagate(remainingEvaluations);
    for (std::size_t i = 0; i < m_outputs.size(); ++i)
        setStateOutPin(static_cast<int>(i), m_circuit.getComponent(m_outputs[i])->getStateInPin(0));
}

void Subcircuit::collectClocks(std::vector<Clock*>& clocks)
{
    m_circuit.collectClocks(clocks);
}
