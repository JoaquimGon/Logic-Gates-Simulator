#include "main.h"
#include "gate.h"
#include "circuit.h"
#include <vector>
#include <iostream>

int main()
{

	// Example circuit of the function (a*b) ^ (c*d)

	Circuit circuit;

	int gate0 = circuit.addGate(GateType::AND); // ID: 0
	int gate1 = circuit.addGate(GateType::AND); // ID: 1
	int gate2 = circuit.addGate(GateType::XOR); // ID: 2
	int gate3 = circuit.addGate(GateType::AND); // ID: 3

	circuit.connectGates(gate0, gate2, 0);
	circuit.connectGates(gate1, gate2, 1);

	if (!circuit.connectGates(gate3, gate2, 1))
	{
		std::cerr << "Tried to connect gate " << gate3 << " to gate " << gate2 << " - Pin already occupied\n";
	}

	bool a, b, c, d;
	a = b = c = d = false;

	std::vector<bool> answer = { 0,0,0,1,0,0,0,1,0,0,0,1,1,1,1,0 };

	for (int i = 0; i < pow(2,4); i++)
	{	
		if (i != 0)
		{
			a = !a;
			if (i % 2 == 0) { b = !b; }
			if (i % 4 == 0) { c = !c; }
			if (i % 8 == 0) { d = !d; }
		}
		circuit.getGate(gate0).setStateInPins(0, a);
		circuit.getGate(gate0).setStateInPins(1, b);
		circuit.getGate(gate1).setStateInPins(0, c);
		circuit.getGate(gate1).setStateInPins(1, d);


		
		if (i == 0) {
			std::vector<int> propagationOrder = circuit.getEvaluationOrder();
			std::cout << "Order: ";
			for (int j = 0; j < 3; j++)
			{
				std::cout << propagationOrder[j] << ", ";
			}
			std::cout << "\nD | C | B | A | f | F\n";
		}

		circuit.propagate();

		// Truth table to terminal
		// F is a hardcoded answer for proof
		std::cout << circuit.getGate(gate1).getStateInPins()[1] << " | ";
		std::cout << circuit.getGate(gate1).getStateInPins()[0] << " | ";
		std::cout << circuit.getGate(gate0).getStateInPins()[1] << " | ";
		std::cout << circuit.getGate(gate0).getStateInPins()[0] << " | ";
		std::cout << circuit.getGate(gate2).getStateOutPin() << " | ";
		std::cout << answer[i]<< "\n";

	}
}