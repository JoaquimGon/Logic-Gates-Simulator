#pragma once
#include "Mesh.h"
#include "Shader.h"
#include "ShaderManager.h"
#include "VertexLayout.h"
#include "..\Circuit.h"

#include <glad/glad.h>
#include <GLFW/glfw3.h>


class Engine 
{
private:
	GLFWwindow* window;
	
	ShaderManager& sm = ShaderManager::instance();

	// Meshes
	std::unique_ptr<Mesh> gateMesh;
	std::unique_ptr<Mesh> lineMesh;

	static void resizeWindow(GLFWwindow* window, int width, int height) 
	{ 
		glViewport(0, 0, width, height); 
	}

	void processInput();
public:
	int init(std::string windowName, int windowWidth, int windowHeight);
	void run();


};