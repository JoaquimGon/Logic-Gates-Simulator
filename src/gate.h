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
	std::vector<Connection> m_connections;

public:
	Gate(GateType gateType, bool outInverted);
	GateType getType();
	std::vector<Connection> getConnections();
	std::vector<bool> getStateInPins();
	void setStateInPins(int pinIndex, bool state);
	bool getStateOutPin();
	void evaluateOut();
	void addConnection(int gateId, int pinIndex);
	bool hasConnection();
};
