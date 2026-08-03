#pragma once
#include "Mesh.h"
#include "Shader.h"
#include "ShaderManager.h"
#include "VertexLayout.h"
#include "..\Circuit.h"
#include "Input.h"

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

class Engine 
{
private:
	bool isDragging = false;
	GLFWwindow* window;
	std::string m_windowName;
	int m_windowWidth = 0;
	int m_windowHeight = 0;

	Input input;

	ShaderManager& sm = ShaderManager::instance();

	// Meshes
	std::unique_ptr<Mesh> gridMesh;
	std::unique_ptr<Mesh> gateMesh;
	std::unique_ptr<Mesh> lineMesh;

	static void resizeWindow(GLFWwindow* window, int width, int height) 
	{ 
		glViewport(0, 0, width, height); 
	}


public:
	Engine(std::string windowName, int windowWidth, int windowHeight);
	int init();
	void run();

	static void mouseButtonCallback(GLFWwindow* window, int button, int action, int mods);
	static void cursorPositionCallback(GLFWwindow* window, double xpos, double ypos);
};