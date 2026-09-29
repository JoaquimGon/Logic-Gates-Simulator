#pragma once
#include "Component.h"

#include <vector>

enum class LatchType
{
    SR_LATCH, // Inputs: [0]=S, [1]=R. Outputs: [0]=Q, [1]=~Q
    D_LATCH   // Inputs: [0]=D, [1]=E (Enable). Outputs: [0]=Q, [1]=~Q
};

class Latch : public Component
{
  public:
    Latch(int id, LatchType type)
        : Component(id), m_type(type), m_stateInPins(type == LatchType::SR_LATCH ? 2 : 2, false)
    {
        m_q = false;
        m_notQ = true;
    }

    void evaluate() override
    {
        if (m_type == LatchType::SR_LATCH)
        {
            bool S = m_stateInPins[0];
            bool R = m_stateInPins[1];

            if (S && !R)
            {
                m_q = true;
                m_notQ = false;
            }
            else if (!S && R)
            {
                m_q = false;
                m_notQ = true;
            }
            else if (S && R)
            {
                // Invalid / Metastable state: both outputs forced low
                m_q = false;
                m_notQ = false;
            }
            // If S=0, R=0: Hold state
        }
        else if (m_type == LatchType::D_LATCH)
        {
            bool D = m_stateInPins[0];
            bool E = m_stateInPins[1];

            if (E)
            {
                // Transparent Mode
                m_q = D;
                m_notQ = !D;
            }
            // If E == false: Latch retains previous state
        }
    }

    bool getStateOutPin(int outIndex = 0) const override { return (outIndex == 0) ? m_q : m_notQ; }

    void setStateInPin(int pinIndex, bool state) override
    {
        if (pinIndex >= 0 && pinIndex < static_cast<int>(m_stateInPins.size()))
            m_stateInPins[pinIndex] = state;
    }

    std::vector<bool> getStateInPins() const override { return m_stateInPins; }

    int getInputPinCount() const override { return static_cast<int>(m_stateInPins.size()); }

    int getOutputPinCount() const override { return 2; }

    LatchType getType() const { return m_type; }

  private:
    LatchType m_type;
    std::vector<bool> m_stateInPins;
    bool m_q = false;
    bool m_notQ = true;
};