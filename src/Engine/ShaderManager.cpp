#include "..\..\include\Engine\ShaderManager.h"


ShaderManager& ShaderManager::instance() {
    static ShaderManager mgr; // Created only once, safely
    return mgr;
}

ShaderManager::ShaderManager() {}

Shader* ShaderManager::load(const std::string& name,
    const std::string& vertexPath,
    const std::string& fragmentPath)
{
    // Already loaded, reuse
    auto it = shaders.find(name);
    if (it != shaders.end()) return it->second.get();

    // Load shader
    auto shader = std::make_unique<Shader>(vertexPath.c_str(), fragmentPath.c_str());
    Shader* ptr = shader.get();
    shaders[name] = std::move(shader);
    return ptr;
}


Shader* ShaderManager::get(const std::string& name) 
{
    auto it = shaders.find(name);
    return (it != shaders.end()) ? it->second.get() : nullptr;
}
