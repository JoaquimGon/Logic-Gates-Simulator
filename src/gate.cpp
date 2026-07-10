#include "gate.h"
#include <iostream>

Gate::Gate(GateType gateType, bool outInverted) 
	: m_gateType(gateType),
	  m_outInverted(outInverted)
{
	// Gate settings
	int inPinsCount{};
	//int outPinsCount{};	

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
		std::cout << "Gate type unspecified\n";
		break;
	}
	
	// Default input pins to LOW
	m_stateInPins.resize(inPinsCount, false);
}

// Getters
GateType Gate::getType() { return m_gateType; }
std::vector<bool> Gate::getStateInPins() { return m_stateInPins; }
void Gate::setStateInPins(int pinIndex, bool state) { m_stateInPins[pinIndex] = state; }
bool Gate::getStateOutPin() { return m_stateOutPin; }
std::vector<Connection> Gate::getOutConnections() { return m_outConnections; }
std::vector<Connection> Gate::getInConnections() { return m_inConnections; }
//


void Gate::evaluateOut()
{
	switch (m_gateType)
	{
	case NOT:
		m_stateOutPin = !m_stateInPins[0];
		break;
	case AND:
		m_stateOutPin = m_stateInPins[0] && m_stateInPins[1];
		break;
	case OR:
		m_stateOutPin = m_stateInPins[0] || m_stateInPins[1];
		break;
	case XOR:
		m_stateOutPin = m_stateInPins[0] ^ m_stateInPins[1];
		break;
	default:
		m_stateOutPin = false;
		break;
	}
	if (m_outInverted)
	{
		m_stateOutPin = !m_stateOutPin;
	}
}

void Gate::addOutConnection(int gateId, int pinIndex)
{
	Connection connection;
	connection.gateId = gateId;
	connection.pinIndex = pinIndex;
	m_outConnections.push_back(connection);
}

void Gate::addInConnection(int gateId, int pinIndex)
{
	Connection connection;
	connection.gateId = gateId;
	connection.pinIndex = pinIndex;
	m_inConnections.push_back(connection);
}

bool Gate::hasConnection()
{
	if (m_outConnections.size() == 0)
	{
		return false;
	}
	else
	{
		return true;
	}
}

