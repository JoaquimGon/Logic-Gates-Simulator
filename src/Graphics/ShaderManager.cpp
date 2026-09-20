#include "ShaderManager.h"

#include <iostream>


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

    // A shader that failed to compile or link is deliberately kept out of the
    // registry instead of being cached: it is unusable, and a later load() after
    // the source has been fixed can still succeed. Callers get nullptr so they can
    // report the problem, rather than a shader that silently draws nothing.
    if (!shader->isValid()) {
        std::cerr << "ERROR::SHADER::BUILD_FAILED: \"" << name << "\" was not registered.\n";
        return nullptr;
    }

    Shader* ptr = shader.get();
    shaders[name] = std::move(shader);
    return ptr;
}


Shader* ShaderManager::get(const std::string& name) 
{
    auto it = shaders.find(name);
    return (it != shaders.end()) ? it->second.get() : nullptr;
}
