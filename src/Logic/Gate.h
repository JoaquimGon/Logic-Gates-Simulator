#pragma once
#include "Component.h"

enum GateType
{
    NOT,
    AND,
    NAND,
    OR,
    NOR,
    XOR,
    NXOR
};

class Gate : public Component
{
  public:
    Gate(int id, GateType gateType);
    Gate(int id, GateType gateType, int inputPinCount);

    /** @brief NOT takes one input; all other gates require at least two. */
    static bool isValidInputPinCount(GateType type, int inputPinCount);

    /** @brief XOR computes odd parity across all inputs; NXOR computes its inverse. */
    void evaluate() override;

    GateType getType() const { return m_gateType; }

  private:
    GateType m_gateType;
};
