#pragma once
#include <vector>

enum GateType
{
	NOT,
	AND,
	OR,
	XOR,
};

struct Connection
{
	int gateId{ -1 };
	int pinIndex{ -1 };
};

class Gate 
{
private:
	GateType m_gateType;
	bool m_dirty{ false };
	bool m_outInverted{ false };
	std::vector<bool> m_stateInPins;
	bool m_stateOutPin{ false };
	std::vector<Connection> m_outConnections;
	std::vector<Connection> m_inConnections;

public:
	Gate(GateType gateType, bool outInverted);
	GateType getType();
	std::vector<Connection> getInConnections();
	std::vector<Connection> getOutConnections();
	std::vector<bool> getStateInPins();
	void setStateInPins(int pinIndex, bool state);
	bool getStateOutPin();
	void evaluateOut();
	void addInConnection(int gateId, int pinIndex);
	void addOutConnection(int gateId, int pinIndex);
	bool hasConnection();
};
