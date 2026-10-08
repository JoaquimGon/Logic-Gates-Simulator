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
    std::unique_ptr<Component> clone() const override { return std::make_unique<Gate>(*this); }

    Gate(int id, GateType gateType);
    Gate(int id, GateType gateType, int inputPinCount);

    /** @brief NOT takes one input; all other gates require at least two. */
    static bool isValidInputPinCount(GateType type, int inputPinCount);

    /** @brief XOR computes odd parity across all inputs; NXOR computes its inverse. */
    void evaluate() override;

    GateType getType() const { return m_gateType; }

    bool isInverted() const
    {
        return m_gateType == NOT || m_gateType == NAND || m_gateType == NOR || m_gateType == NXOR;
    }

    /** @brief Selects AND/NAND, OR/NOR, or XOR/NXOR without replacing pins or state.
     * NOT has fixed inversion and rejects this operation.
     */
    void setInverted(bool inverted);
    static GateType typeWithInversion(GateType type, bool inverted);

  private:
    GateType m_gateType;
};
