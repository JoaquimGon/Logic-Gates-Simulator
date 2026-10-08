#include "Shader.h"

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <vector>

#ifndef PROJECT_ASSETS_DIR
#define PROJECT_ASSETS_DIR "assets"
#endif

namespace
{
std::optional<std::filesystem::file_time_type> sourceTime(const std::string& path)
{
    std::error_code error;
    const auto time = std::filesystem::last_write_time(path, error);
    return error ? std::nullopt : std::optional{time};
}
} // namespace

std::string Shader::resolvePath(const std::string& path)
{
    std::filesystem::path p(path);

    // If it's already absolute, leave it as is
    if (p.is_absolute())
    {
        return p.string();
    }

    // Combine CMake's project asset root with the relative path
    std::filesystem::path fullPath = std::filesystem::path(PROJECT_ASSETS_DIR) / p;
    return fullPath.lexically_normal().string();
}

bool Shader::buildProgram(bool isReload)
{
    m_attemptedSources.clear();
    m_attemptedSources[m_vertPath] = sourceTime(m_vertPath);
    m_attemptedSources[m_fragPath] = sourceTime(m_fragPath);
    std::vector<std::string> vertexFiles, fragmentFiles, stack;
    std::string vCode, fCode;
    try
    {
        vCode = readFile(m_vertPath, vertexFiles, stack);
        fCode = readFile(m_fragPath, fragmentFiles, stack);
    }
    catch (const std::runtime_error& error)
    {
        std::cerr << "ERROR::SHADER::SOURCE: " << error.what() << '\n';
        return false;
    }
    unsigned int vertexShader = compile(vCode.c_str(), GL_VERTEX_SHADER, "VERTEX", vertexFiles);
    unsigned int fragmentShader =
        compile(fCode.c_str(), GL_FRAGMENT_SHADER, "FRAGMENT", fragmentFiles);

    if (vertexShader == 0 || fragmentShader == 0)
    {
        if (vertexShader != 0)
            glDeleteShader(vertexShader);
        if (fragmentShader != 0)
            glDeleteShader(fragmentShader);
        return false; // Retain current program ID so window doesn't go blank on
                      // syntax errors
    }

    unsigned int newID = glCreateProgram();
    glAttachShader(newID, vertexShader);
    glAttachShader(newID, fragmentShader);
    glLinkProgram(newID);

    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    if (!checkLinkErrors(newID))
    {
        glDeleteProgram(newID);
        return false;
    }

    // Hot-swap the program handle safely
    if (ID != 0)
    {
        glDeleteProgram(ID);
    }
    ID = newID;

    m_uniformLocations.clear(); // Locations belong to the newly linked program.

    if (isReload)
    {
        std::cout << "[Shader] Successfully reloaded: " << m_fragPath << std::endl;
    }

    return true;
}

Shader::Shader(const char* vertexPath, const char* fragmentPath)
{
    // Resolve both paths to machine-independent absolute paths
    m_vertPath = resolvePath(vertexPath);
    m_fragPath = resolvePath(fragmentPath);

    buildProgram(false);
}

void Shader::checkAndReload()
{
    if (m_vertPath.empty() || m_fragPath.empty())
        return;

    // Throttle checks to ~5 times a second per shader instance
    auto now = std::chrono::steady_clock::now();
    if (std::chrono::duration_cast<std::chrono::milliseconds>(now - m_lastCheckTime).count() < 200)
    {
        return;
    }
    m_lastCheckTime = now;

    for (const auto& [path, attemptedTime] : m_attemptedSources)
    {
        if (sourceTime(path) != attemptedTime)
        {
            buildProgram(true);
            break;
        }
    }
}

void Shader::use() const
{
    if (ID == 0)
        return;
    glUseProgram(ID);
}

int Shader::uniformLocation(const std::string& name) const
{
    if (ID == 0)
        return -1;
    const auto found = m_uniformLocations.find(name);
    if (found != m_uniformLocations.end())
        return found->second;
    const int location = glGetUniformLocation(ID, name.c_str());
    m_uniformLocations.emplace(name, location); // Cache -1 as well.
    return location;
}

void Shader::setBool(const std::string& name, bool value) const
{
    glUniform1i(uniformLocation(name), (int)value);
}

void Shader::setFloat(const std::string& name, float value) const
{
    glUniform1f(uniformLocation(name), value);
}

void Shader::setMat3(const std::string& name, const glm::mat3& mat) const
{
    glUniformMatrix3fv(uniformLocation(name), 1, GL_FALSE, &mat[0][0]);
}

void Shader::setMat4(const std::string& name, const glm::mat4& mat) const
{
    glUniformMatrix4fv(uniformLocation(name), 1, GL_FALSE, &mat[0][0]);
}

void Shader::setVec2(const std::string& name, float value1, float value2) const
{
    glUniform2f(uniformLocation(name), value1, value2);
}

void Shader::setVec4(
    const std::string& name, float value1, float value2, float value3, float value4
) const
{
    glUniform4f(uniformLocation(name), value1, value2, value3, value4);
}

Shader::~Shader()
{
    if (ID != 0)
        glDeleteProgram(ID);
}

unsigned int Shader::compile(
    const char* src, GLenum type, const char* stageName, const std::vector<std::string>& files
)
{
    unsigned int stage = glCreateShader(type);
    glShaderSource(stage, 1, &src, NULL);
    glCompileShader(stage);

    int success = GL_FALSE;
    glGetShaderiv(stage, GL_COMPILE_STATUS, &success);
    if (success == GL_TRUE)
        return stage;

    GLint logLength = 0;
    glGetShaderiv(stage, GL_INFO_LOG_LENGTH, &logLength);
    std::vector<char> infoLog(logLength > 1 ? static_cast<size_t>(logLength) : 1, '\0');
    glGetShaderInfoLog(stage, static_cast<GLsizei>(infoLog.size()), NULL, infoLog.data());

    std::cerr << "ERROR::SHADER::" << stageName << "::COMPILATION_FAILED\n"
              << infoLog.data() << '\n';
    // GLSL diagnostics identify files by the numeric IDs supplied through #line.
    for (std::size_t i = 0; i < files.size(); ++i)
        std::cerr << " source " << i << ": " << files[i] << '\n';

    glDeleteShader(stage);
    return 0;
}

std::string Shader::readFile(
    const std::string& path, std::vector<std::string>& files, std::vector<std::string>& stack
)
{
    m_attemptedSources[path] = sourceTime(path);
    if (std::find(stack.begin(), stack.end(), path) != stack.end())
        throw std::runtime_error("Cyclic shader include: " + path);
    std::ifstream file(path);
    if (!file)
        throw std::runtime_error("Cannot read shader: " + path);
    const auto found = std::find(files.begin(), files.end(), path);
    const auto sourceId = found == files.end() ? files.size() : found - files.begin();
    if (found == files.end())
        files.push_back(path);
    stack.push_back(path);
    std::ostringstream source;
    std::string line;
    int lineNumber = 0;
    while (std::getline(file, line))
    {
        ++lineNumber;
        std::istringstream directive(line);
        std::string token;
        directive >> token;
        if (token != "#include")
        {
            source << line << '\n';
            continue;
        }
        directive >> std::ws;
        std::string relative, trailing;
        if (directive.peek() != '"' || !(directive >> std::quoted(relative)))
            throw std::runtime_error(
                "Expected quoted include at " + path + ":" + std::to_string(lineNumber)
            );
        std::getline(directive >> std::ws, trailing);
        if (!trailing.empty() && !trailing.starts_with("//"))
            throw std::runtime_error(
                "Invalid include at " + path + ":" + std::to_string(lineNumber)
            );
        const auto child =
            (std::filesystem::path(path).parent_path() / relative).lexically_normal().string();
        // readFile registers a stable source ID before expanding nested includes.
        const auto childFound = std::find(files.begin(), files.end(), child);
        const auto childId = childFound == files.end() ? files.size() : childFound - files.begin();
        source << "#line 1 " << childId << '\n'
               << readFile(child, files, stack) << "#line " << lineNumber + 1 << ' ' << sourceId
               << '\n';
    }
    if (file.bad())
        throw std::runtime_error("Failed reading shader: " + path);
    stack.pop_back();
    return source.str();
}

bool Shader::checkLinkErrors(unsigned int program)
{
    int success = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (success == GL_TRUE)
        return true;

    GLint logLength = 0;
    glGetProgramiv(program, GL_INFO_LOG_LENGTH, &logLength);
    std::vector<char> infoLog(logLength > 1 ? static_cast<size_t>(logLength) : 1, '\0');
    glGetProgramInfoLog(program, static_cast<GLsizei>(infoLog.size()), NULL, infoLog.data());

    std::cerr << "ERROR::SHADER::PROGRAM::LINKING_FAILED\n" << infoLog.data() << std::endl;
    return false;
}
