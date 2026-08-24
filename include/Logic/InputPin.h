#pragma once
#include "Component.h"

// A manually-toggled boolean source. No inputs, one output, and evaluate()
// does nothing because its state is driven by the user, not by upstream logic.
class InputPin : public Component
{
public:
    InputPin(int id, bool initialState = false) : Component(id), m_state(initialState) {}

    void evaluate() override { /* no-op: state is externally driven */ }
    bool getStateOutPin(int /*outIndex*/ = 0) const override { return m_state; }
    void setStateInPin(int, bool) override { /* no input pins to set */ }
    std::vector<bool> getStateInPins() const override { return {}; }
    int getInputPinCount()  const override { return 0; }
    int getOutputPinCount() const override { return 1; }

    void toggle() { m_state = !m_state; }
    void setState(bool s) { m_state = s; }
    bool getState() const { return m_state; }

private:
    bool m_state;
};