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

class Gate {
private:
	GateType m_gateType;
	bool m_dirty{ false };
	bool m_outInverted{ false };
	std::vector<bool> m_inPins;
	bool m_outPin{ false };
	std::vector<Connection> m_inConnections;
	std::vector<Connection> m_outConnections;

public:

	Gate(GateType gateType, bool outInverted);
	void evaluateOut();
	void createConnection(int gateId, int pinIndex);
	bool hasInput();
};
