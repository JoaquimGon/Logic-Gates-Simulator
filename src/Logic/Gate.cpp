#include "..\..\include\Logic\Gate.h"
#include <iostream>

Gate::Gate(int id, GateType gateType, bool outInverted)
    : Component(id),
    m_gateType(gateType),
    m_outInverted(gateType == GateType::NOT ? false : outInverted)
{
    if (gateType == GateType::NOT && outInverted) {
        std::cerr << "[Gate Warning]: A NOT gate cannot have an inverted output. Defaulting to false.\n";
    }

    int inPinsCount = 0;
    switch (m_gateType) {
    case NOT: inPinsCount = 1; break;
    case AND: case OR: case XOR: inPinsCount = 2; break;
    default: std::cerr << "Gate type unspecified\n"; break;
    }
    m_stateInPins.resize(inPinsCount, false);
}

void Gate::setStateInPin(int pinIndex, bool state)
{
    if (pinIndex >= 0 && pinIndex < static_cast<int>(m_stateInPins.size()))
        m_stateInPins[pinIndex] = state;
    else
        std::cerr << "[Gate Error]: Invalid pin index " << pinIndex << "\n";
}

void Gate::evaluate()
{
    switch (m_gateType) {
    case NOT: m_stateOutPin = !m_stateInPins[0]; break;
    case AND: m_stateOutPin = m_stateInPins[0] && m_stateInPins[1]; break;
    case OR:  m_stateOutPin = m_stateInPins[0] || m_stateInPins[1]; break;
    case XOR: m_stateOutPin = m_stateInPins[0] ^ m_stateInPins[1]; break;
    default:  m_stateOutPin = false; break;
    }
    if (m_outInverted) m_stateOutPin = !m_stateOutPin;
}