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
	int m_id;
	GateType m_gateType;
	bool m_dirty{ false };
	bool m_outInverted{ false };
	std::vector<bool> m_stateInPins;
	bool m_stateOutPin{ false };
	std::vector<Connection> m_outConnections;
	std::vector<Connection> m_inConnections;

public:
	Gate(int id, GateType gateType, bool outInverted);
	GateType getType() const;
	std::vector<Connection> getInConnections() const;
	std::vector<Connection> getOutConnections() const;
	std::vector<bool> getStateInPins();
	void setStateInPins(int pinIndex, bool state);
	bool getStateOutPin();
	void evaluateOut();
	void addInConnection(int gateId, int pinIndex);
	void delInConnection(int srcGateId, int srcPinIndex);
	void addOutConnection(int gateId, int pinIndex);
	void delOutConnection(int destGateId, int destPinIndex);
	bool hasConnection();
};
