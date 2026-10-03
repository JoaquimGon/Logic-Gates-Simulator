#pragma once
#include "Component.h"

/** A passive circuit output: one input, no outgoing signal pins. */
class OutputPin : public Component
{
  public:
    explicit OutputPin(int id) : Component(id, 1, 0) {}

    std::unique_ptr<Component> clone() const override { return std::make_unique<OutputPin>(*this); }

    void evaluate() override {}

    bool getState() const { return getStateInPin(0); }
};
