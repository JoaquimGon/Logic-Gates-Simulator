#pragma once
#include "Components/Component.h"

enum class FlipFlopType
{
    D,
    T,
    JK
};

/** One stored bit, sampled on CLK's rising edge. Outputs start at Q=0, ~Q=1. */
class FlipFlop : public Component
{
  public:
    FlipFlop(int id, FlipFlopType type)
        : Component(id, type == FlipFlopType::JK ? 3 : 2, 2), m_type(type)
    {
        if (type != FlipFlopType::D && type != FlipFlopType::T && type != FlipFlopType::JK)
            throw std::invalid_argument("Invalid flip-flop type.");
        setStateOutPin(1, true);
    }

    std::unique_ptr<Component> clone() const override { return std::make_unique<FlipFlop>(*this); }

    void evaluate() override {}

    int clockInputPin() const override { return getInputPinCount() - 1; }

    FlipFlopType getType() const { return m_type; }

    void onClockEdge(bool high) override
    {
        if (!high)
            return;
        bool next = getStateOutPin(0);
        switch (m_type)
        {
        case FlipFlopType::D:
            next = getStateInPin(0);
            break;
        case FlipFlopType::T:
            if (getStateInPin(0))
                next = !next;
            break;
        case FlipFlopType::JK:
            if (getStateInPin(0) != getStateInPin(1))
                next = getStateInPin(0);
            else if (getStateInPin(0))
                next = !next;
            break;
        }
        setStateOutPin(0, next);
        setStateOutPin(1, !next);
    }

  private:
    FlipFlopType m_type;
};
