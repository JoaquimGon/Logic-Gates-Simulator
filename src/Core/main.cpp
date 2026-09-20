#include "Engine.h"

#include <iostream>
#include <glad/glad.h>
#include <GLFW/glfw3.h>


int main()
{
	Engine engine("Logic Gate Simulator", 800, 600);

	// init() owns the GLFW window and the GL context, and run() drives a loop that
	// needs both. Ignoring this result used to hand run() a null window.
	if (engine.init() != 0)
	{
		std::cerr << "Engine initialization failed; exiting.\n";
		return -1;
	}

	engine.run();
	return 0;
}
