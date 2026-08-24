#include "Engine.h"

#include <iostream>
#include <glad/glad.h>
#include <GLFW/glfw3.h>


int main()
{
	Engine engine("Logic Gate Simulator", 800, 600);
	engine.init();
	engine.run();
}
