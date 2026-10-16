#pragma once

#include <chrono>
#include <filesystem>
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

/**
 * @brief Class that handles shader files, compilation, linking, and live
 * hot-reloading.
 */
class Shader
{
  private:
    /**
     * @brief Compiles one shader stage and reports any compiler diagnostics.
     * @param stageName Stage tag used in error messages, e.g. "VERTEX".
     * @param files Source ID to path mapping used in compiler diagnostics.
     * @return The compiled stage object, or 0 when compilation failed.
     */
    unsigned int compile(const char* src, GLenum type, const char* stageName, const std::vector<std::string>& files);

    /**
     * @brief Expands quoted relative includes and retains source lines/dependencies.
     * @param files Source ID to path mapping for this stage.
     * @param stack Active include chain for detecting cycles.
     * @return Expanded GLSL; throws when reading or include expansion fails.
     */
    std::string readFile(const std::string& path, std::vector<std::string>& files, std::vector<std::string>& stack);

    /**
     * @brief Converts a relative asset path into an absolute file path using
     * the CMake project root.
     */
    static std::string resolvePath(const std::string& path);

    /** @brief Report linker diagnostics when a program fails to link. */
    bool checkLinkErrors(unsigned int program);

    /**
     * @brief Resolves a uniform location, tolerating an unusable program.
     * @return The uniform location, or -1 when the program is invalid or the
     * uniform does not exist. glUniform*() treats -1 as a no-op, so callers can
     * pass it straight through.
     */
    int uniformLocation(const std::string& name) const;

    unsigned int ID = 0; // OpenGL program handle; 0 means the shader is unusable

    // Hot-reload tracking
    std::string m_vertPath = "";
    std::string m_fragPath = "";
    // Track the last attempted source revision, including missing includes.
    std::unordered_map<std::string, std::optional<std::filesystem::file_time_type>> m_attemptedSources;
    mutable std::unordered_map<std::string, int> m_uniformLocations;

    std::chrono::steady_clock::time_point m_lastCheckTime{}; // Per-instance poll throttle

    /**
     * @brief Internal compiler and linker pipeline that safe-swaps GL program
     * IDs.
     */
    bool buildProgram(bool isReload = false);

  public:
    Shader(const Shader&) = delete;
    Shader& operator=(const Shader&) = delete;

    /** @brief Resolve asset paths and build a program; isValid() reports compilation or linking failure. */
    Shader(const char* vertexPath, const char* fragmentPath);

    /**
     * @brief Checks if source files on disk have been edited, recompiling if
     * needed.
     */
    void checkAndReload();

    /**
     * @brief Points OpenGL to use this shader. Does nothing when the shader
     * could not be built (see isValid()).
     */
    void use() const;

    bool isValid() const { return ID != 0; }

    ~Shader();

    void setBool(const std::string& name, bool value) const;

    void setFloat(const std::string& name, float value) const;

    void setMat3(const std::string& name, const glm::mat3& mat) const;

    void setMat4(const std::string& name, const glm::mat4& mat) const;

    void setVec2(const std::string& name, float value1, float value2) const;

    void setVec4(const std::string& name, float value1, float value2, float value3, float value4) const;
};
