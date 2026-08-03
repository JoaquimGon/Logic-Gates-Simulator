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
	std::string m_windowName;
	int m_windowWidth = 0;
	int m_windowHeight = 0;

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
	Engine(std::string windowName, int windowWidth, int windowHeight);
	int init();
	void run();


};