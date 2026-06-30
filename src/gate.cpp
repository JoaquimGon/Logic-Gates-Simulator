#include "gate.h"

Gate::Gate(GateType gateType, bool outInverted) 
	: m_gateType(gateType),
	  m_outInverted(outInverted)
{
	// Gate settings
	int inPinsCount{};
	int outPinsCount{};	

	switch (m_gateType) 
	{
	case NOT:
		inPinsCount = 1;
		//outPinsCount = 1;
		break;
	case AND:
	case OR:
	case XOR:
		inPinsCount = 2;
		//outPinsCount = 1;
		break;
	default:
		inPinsCount = 0;
		//outPinsCount = 0;
		break;
	}
	
	m_inPins.resize(inPinsCount, false);	
}

void Gate::evaluateOut()
{
	switch (m_gateType)
	{
	case NOT:
		m_outPin = !m_inPins[0];
		break;
	case AND:
		m_outPin = m_inPins[0] && m_inPins[1];
		break;
	case OR:
		m_outPin = m_inPins[0] || m_inPins[1];
		break;
	case XOR:
		m_outPin = m_inPins[0] ^ m_inPins[1];
		break;
	default:
		m_outPin = false;
		break;
	}
	if (m_outInverted)
	{
		m_outPin = !m_outPin;
	}
}

bool Gate::hasInput()
{
	if (m_inConnections[0].gateId == -1)
	{
		return false;
	}
	else
	{
		return true;
	}
}
