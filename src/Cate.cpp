#include "gate.h"
#include <iostream>

Gate::Gate(int id, GateType gateType, bool outInverted) 
	: m_id(id),
	  m_gateType(gateType),
	  m_outInverted(gateType == GateType::NOT ? false : outInverted)
{

	if (gateType == GateType::NOT && outInverted)
	{
		std::cerr << "[Gate Warning]: A NOT gate cannot have an inverted output. "
			<< "Defaulting output inversion to false.\n";
	}

	// Gate settings
	int inPinsCount{};

	switch (m_gateType) 
	{
	case NOT:
		inPinsCount = 1;
		break;
	case AND:
	case OR:
	case XOR:
		inPinsCount = 2;
		break;
	default:
		inPinsCount = 0;
		std::cerr << "Gate type unspecified\n";
		break;
	}
	
	// Default input pins to LOW
	m_stateInPins.resize(inPinsCount, false);
}

// Getters
GateType Gate::getType() const { return m_gateType; }
std::vector<bool> Gate::getStateInPins() { return m_stateInPins; }
void Gate::setStateInPins(int pinIndex, bool state) { m_stateInPins[pinIndex] = state; }
bool Gate::getStateOutPin() { return m_stateOutPin; }
std::vector<Connection> Gate::getOutConnections() const { return m_outConnections; }
std::vector<Connection> Gate::getInConnections() const { return m_inConnections; }
//


void Gate::evaluateOut()
{
	if (m_gateType != NOT && !(m_stateInPins.size() >= 2))
	{
		std::cerr << "Gate does not have the necessary input pins.";
	}
	else if (m_gateType == NOT && !(m_stateInPins.size() > 1))
	{
		std::cerr << "Gate has more input pins than it should.";
	}

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

void Gate::delOutConnection(int destGateId, int destPinIndex)
{
	std::erase_if(m_outConnections, [destGateId, destPinIndex](const Connection& conn) {
		return conn.gateId == destGateId && conn.pinIndex == destPinIndex;
		});
}

void Gate::delInConnection(int srcGateId, int srcPinIndex)
{
	std::erase_if(m_inConnections, [srcGateId, srcPinIndex](const Connection& conn) {
		return conn.gateId == srcGateId && conn.pinIndex == srcPinIndex;
		});
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

