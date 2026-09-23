#pragma once
#include "Shader.h"

#include <vector>
#include <string>
#include <unordered_map>
#include <memory>

/**
* @brief Singleton Manager class of the shader class, safely loads and gets existing shaders
*/
class ShaderManager {
private:
	std::unordered_map<std::string, std::unique_ptr<Shader>> shaders;

public:

	ShaderManager();
	~ShaderManager() = default;

	/*
	* @brief Loads a shader (vertex and fragment), will search if it's already loaded first
	* @param name Name for the shader to be used
	* @param vertexPath File path for the vertex shader
	* @param fragmentPath File path for the fragment shader
	*/
	Shader* load(const std::string& name,
		const std::string& vertexPath,
		const std::string& fragmentPath);

	/*
	* @brief Gets a shader using the name
	* @param name Name of the shader
	*/
	Shader* get(const std::string& name);

	/*
	* @brief Destroys every loaded shader program.
	* Needs a current OpenGL context, so call it before the context goes away; the
	* manager is left empty and can still be reused afterwards.
	*/
	void release();

	/*
	* @brief Checks if files were altered, if yes trigger shader reload
	*/
	void checkHotReload();
};