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
    : Component(id, checkedInputPinCount(gateType, inputPinCount), 1), m_gateType(gateType)
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

void Gate::evaluate()
{
    const auto& inputs = getStateInPins();
    bool output = false;

    switch (m_gateType)
    {
    case NOT:
        output = !inputs[0];
        break;
    case AND:
    case NAND:
        output = std::all_of(inputs.begin(), inputs.end(), [](bool state) { return state; });
        if (m_gateType == NAND)
            output = !output;
        break;
    case OR:
    case NOR:
        output = std::any_of(inputs.begin(), inputs.end(), [](bool state) { return state; });
        if (m_gateType == NOR)
            output = !output;
        break;
    case XOR:
    case NXOR:
        output = (std::count(inputs.begin(), inputs.end(), true) % 2) != 0;
        if (m_gateType == NXOR)
            output = !output;
        break;
    }

    setStateOutPin(0, output);
}
