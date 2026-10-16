#pragma once
#include "Shader.h"

#include <memory>
#include <string>
#include <unordered_map>

/** @brief Owns a named shader cache; all programs must be released before their OpenGL context ends. */
class ShaderManager
{
  private:
    std::unordered_map<std::string, std::unique_ptr<Shader>> shaders;

  public:
    ShaderManager();
    ~ShaderManager() = default;

    /** @brief Reuse a cached shader or load it; return nullptr on compilation or linking failure. */
    Shader* load(const std::string& name, const std::string& vertexPath, const std::string& fragmentPath);

    Shader* get(const std::string& name);

    /**
     * @brief Destroys every loaded shader program.
     * Needs a current OpenGL context, so call it before the context goes away;
     * the manager is left empty and can still be reused afterwards.
     */
    void release();

    /** @brief Poll source dependencies and retain the current program when a reload fails. */
    void checkHotReload();
};
