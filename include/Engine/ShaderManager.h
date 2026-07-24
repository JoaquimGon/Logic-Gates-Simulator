#pragma once

#include "Shader.h"
#include <vector>
#include <string>
#include <unordered_map>
#include <memory>

class ShaderManager {
private:
	std::unordered_map<std::string, std::unique_ptr<Shader>> shaders;
	ShaderManager();
	~ShaderManager() = default;
public:
	static ShaderManager& instance();
	Shader* load(const std::string& name,
		const std::string& vertexPath,
		const std::string& fragmentPath);
	Shader* get(const std::string& name);

};