#include "Gate.h"

#include <algorithm>
#include <stdexcept>

namespace
{
int checkedInputPinCount(GateType type, int inputPinCount)
{
    if (!Gate::isValidInputPinCount(type, inputPinCount))
        throw std::invalid_argument("Invalid gate type or input pin count.");
    return inputPinCount;
}
} // namespace


Gate::Gate(int id, GateType gateType) : Gate(id, gateType, gateType == NOT ? 1 : 2) {}


Gate::Gate(int id, GateType gateType, int inputPinCount)
    : Component(id, checkedInputPinCount(gateType, inputPinCount), 1), m_gateType(gateType),
      m_invertedInputs(inputPinCount, false)
{
}


bool Gate::isValidInputPinCount(GateType type, int inputPinCount)
{
    switch (type)
    {
    case NOT:
        return inputPinCount == 1;
    case AND:
    case NAND:
    case OR:
    case NOR:
    case XOR:
    case NXOR:
        return inputPinCount >= 2;
    default:
        return false;
    }
}


void Gate::setInverted(bool inverted)
{
    m_gateType = typeWithInversion(m_gateType, inverted);
}


GateType Gate::typeWithInversion(GateType type, bool inverted)
{
    switch (type)
    {
    case AND:
    case NAND:
        return inverted ? NAND : AND;
    case OR:
    case NOR:
        return inverted ? NOR : OR;
    case XOR:
    case NXOR:
        return inverted ? NXOR : XOR;
    default:
        throw std::invalid_argument("NOT has fixed inversion.");
    }
}


void Gate::evaluate()
{
    const auto& inputs = getStateInPins();
    std::size_t high = 0;
    for (std::size_t i = 0; i < inputs.size(); ++i)
        high += inputs[i] != m_invertedInputs[i];
    bool output = false;

    switch (m_gateType)
    {
    case NOT:
        output = high == 0;
        break;
    case AND:
    case NAND:
        output = high == inputs.size();
        if (m_gateType == NAND)
            output = !output;
        break;
    case OR:
    case NOR:
        output = high != 0;
        if (m_gateType == NOR)
            output = !output;
        break;
    case XOR:
    case NXOR:
        output = high % 2 != 0;
        if (m_gateType == NXOR)
            output = !output;
        break;
    }

    setStateOutPin(0, output);
}
