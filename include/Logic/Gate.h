#pragma once
#include "Component.h"
#include <vector>

enum GateType { NOT, AND, OR, XOR };

class Gate : public Component
{
public:
    Gate(int id, GateType gateType, bool outInverted);

    void evaluate() override;
    bool getStateOutPin(int outIndex = 0) const override { return m_stateOutPin; }
    void setStateInPin(int pinIndex, bool state) override;
    std::vector<bool> getStateInPins() const override { return m_stateInPins; }
    int getInputPinCount()  const override { return static_cast<int>(m_stateInPins.size()); }
    int getOutputPinCount() const override { return 1; }

    GateType getType() const { return m_gateType; }

private:
    GateType m_gateType;
    bool m_outInverted;
    std::vector<bool> m_stateInPins;
    bool m_stateOutPin = false;
};