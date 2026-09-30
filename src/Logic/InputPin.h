#pragma once
#include "Component.h"

// A manual boolean source with no inputs and one output.
class InputPin : public Component
{
  public:
    InputPin(int id, bool initialState = false) : Component(id, 0, 1) { setState(initialState); }

    void evaluate() override {}

    void toggle() { setState(!getState()); }

    void setState(bool state) { setStateOutPin(0, state); }

    bool getState() const { return getStateOutPin(0); }
};
