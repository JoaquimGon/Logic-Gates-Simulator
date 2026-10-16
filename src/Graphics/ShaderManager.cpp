#include "ShaderManager.h"

#include <iostream>
#include <utility>


ShaderManager::ShaderManager() {}


Shader* ShaderManager::load(const std::string& name, const std::string& vertexPath, const std::string& fragmentPath)
{
    auto it = shaders.find(name);
    if (it != shaders.end())
        return it->second.get();

    auto shader = std::make_unique<Shader>(vertexPath.c_str(), fragmentPath.c_str());

    // Cache only usable programs so fixing a source file allows a subsequent load to retry.
    if (!shader->isValid())
    {
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


void ShaderManager::release()
{
    // The owning context must remain current while cached programs are destroyed.
    shaders.clear();
}


void ShaderManager::checkHotReload()
{
    for (auto& [name, shader] : shaders)
    {
        if (shader)
        {
            shader->checkAndReload();
        }
    }
}
