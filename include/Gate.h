#pragma once

#include <vector>
#include <iostream>

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

/*
* @brief Class that holds information pertainig of the gate itself.
*/
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
	/*
	* @brief Initializes a Gate object
	* @param gateType Type of the gate (NOT, AND, OR, XOR)
	* @outInverted If the output is inverted
	*/
	Gate(int id, GateType gateType, bool outInverted);
	
	/*
	* @brief Gets the type of the gate.
	*/
	GateType getType() const;

	/*
	* @brief Gets the input connections of the gate.
	*/
	std::vector<Connection> getInConnections() const;

	/*
	* @brief Gets the output connections of the gate.
	*/
	std::vector<Connection> getOutConnections() const;

	/*
	* @brief Gets the state of the input pins.
	*/
	std::vector<bool> getStateInPins();

	/*
	* @brief Sets the state of the input pins.
	* @param pinIndex Index of the input pin.
	* @param state State to be assigned to the pin.
	*/
	void setStateInPins(int pinIndex, bool state);

	/*
	* @brief Get the state of the output pins
	*/
	bool getStateOutPin();

	/*
	* @brief Evalutes the output pin based on the type of gate and input pins
	*/
	void evaluateOut();

	/*
	* @brief Adds a input conection
	* @brief gateId ID of the source gate
	* @brief pinIndex Index of the input pin it's going to connect to
	*/
	void addInConnection(int gateId, int pinIndex);

	/*
	* @brief Deletes a input connection
	*/
	void delInConnection(int srcGateId, int srcPinIndex);
	void addOutConnection(int gateId, int pinIndex);
	void delOutConnection(int destGateId, int destPinIndex);
	bool hasConnection();
};
