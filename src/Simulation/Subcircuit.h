#pragma once
#include "Components/Definitions/ComponentDefinition.h"
#include "Simulation/Circuit.h"

/** A private simulation per placement; the catalog retains the editable design. */
class Subcircuit : public Component
{
  public:
    explicit Subcircuit(const SubcircuitBehavior& design);
    std::unique_ptr<Component> clone() const override;
    void evaluate() override;
    void evaluate(std::size_t& remainingEvaluations) override;
    void collectClocks(std::vector<Clock*>& clocks) override;

    SimulationResult simulationResult() const override { return m_circuit.getLastEvalResult(); }

  private:
    Circuit m_circuit;
    std::vector<int> m_inputs, m_outputs;
};
