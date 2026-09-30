#pragma once
#include "Component.h"

enum class LatchType
{
    SR_LATCH, // Inputs: S, R. Outputs: Q, ~Q.
    D_LATCH   // Inputs: D, Enable. Outputs: Q, ~Q.
};

class Latch : public Component
{
  public:
    Latch(int id, LatchType type) : Component(id, 2, 2), m_type(type)
    {
        if (type != LatchType::SR_LATCH && type != LatchType::D_LATCH)
            throw std::invalid_argument("Invalid latch type.");
        setStateOutPin(1, true);
    }

    void evaluate() override
    {
        if (m_type == LatchType::SR_LATCH)
        {
            bool set = getStateInPin(0);
            bool reset = getStateInPin(1);

            if (set || reset)
            {
                // S=R=1 is invalid and forces both outputs low; S=R=0 holds state.
                setStateOutPin(0, set && !reset);
                setStateOutPin(1, reset && !set);
            }
        }
        else if (getStateInPin(1))
        {
            // A D latch is transparent while enabled and otherwise retains state.
            bool data = getStateInPin(0);
            setStateOutPin(0, data);
            setStateOutPin(1, !data);
        }
    }

    LatchType getType() const { return m_type; }

  private:
    LatchType m_type;
};
