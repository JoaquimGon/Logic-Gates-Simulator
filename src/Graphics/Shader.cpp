#include "Shader.h"
#include <vector>

#ifndef PROJECT_ASSETS_DIR
#define PROJECT_ASSETS_DIR "assets"
#endif

std::string Shader::resolvePath(const std::string& path)
{
    std::filesystem::path p(path);

    // If it's already absolute, leave it as is
    if (p.is_absolute()) {
        return p.string();
    }

    // Combine CMake's project asset root with the relative path
    std::filesystem::path fullPath = std::filesystem::path(PROJECT_ASSETS_DIR) / p;
    return fullPath.lexically_normal().string();
}

bool Shader::buildProgram(bool isReload)
{
    std::string vCode = readFile(m_vertPath.c_str());
    std::string fCode = readFile(m_fragPath.c_str());

    if (vCode.empty() || fCode.empty()) {
        std::cerr << "ERROR::SHADER::BUILD_SKIPPED: missing source for \""
            << m_vertPath << "\" / \"" << m_fragPath << "\".\n";
        return false;
    }

    unsigned int vertexShader = compile(vCode.c_str(), GL_VERTEX_SHADER, "VERTEX", m_vertPath.c_str());
    unsigned int fragmentShader = compile(fCode.c_str(), GL_FRAGMENT_SHADER, "FRAGMENT", m_fragPath.c_str());

    if (vertexShader == 0 || fragmentShader == 0) {
        if (vertexShader != 0) glDeleteShader(vertexShader);
        if (fragmentShader != 0) glDeleteShader(fragmentShader);
        return false; // Retain current program ID so window doesn't go blank on syntax errors
    }

    unsigned int newID = glCreateProgram();
    glAttachShader(newID, vertexShader);
    glAttachShader(newID, fragmentShader);
    glLinkProgram(newID);

    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    if (!checkLinkErrors(newID)) {
        glDeleteProgram(newID);
        return false;
    }

    // Hot-swap the program handle safely
    if (ID != 0) {
        glDeleteProgram(ID);
    }
    ID = newID;

    // Cache initial/updated write times
    std::error_code ecV, ecF;
    m_lastVertTime = std::filesystem::last_write_time(m_vertPath, ecV);
    m_lastFragTime = std::filesystem::last_write_time(m_fragPath, ecF);

    if (isReload) {
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
    if (m_vertPath.empty() || m_fragPath.empty()) return;

    // Throttle checks to ~5 times a second per shader instance
    auto now = std::chrono::steady_clock::now();
    if (std::chrono::duration_cast<std::chrono::milliseconds>(now - m_lastCheckTime).count() < 200) {
        return;
    }
    m_lastCheckTime = now;

    std::error_code ecV, ecF;
    auto curVertTime = std::filesystem::last_write_time(m_vertPath, ecV);
    auto curFragTime = std::filesystem::last_write_time(m_fragPath, ecF);

    // If an editor holds a temporary write lock, wait for next frame
    if (ecV || ecF) return;

    if (curVertTime > m_lastVertTime || curFragTime > m_lastFragTime) {
        std::cout << "[Shader] Disk modification detected in: " << m_fragPath << '\n';
        buildProgram(true);
    }
}

void Shader::use() const
{
    if (ID == 0) return;
    glUseProgram(ID);
}

int Shader::uniformLocation(const std::string& name) const
{
    return (ID == 0) ? -1 : glGetUniformLocation(ID, name.c_str());
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

void Shader::setVec4(const std::string& name, float value1, float value2, float value3, float value4) const
{
    glUniform4f(uniformLocation(name), value1, value2, value3, value4);
}

Shader::~Shader()
{
    if (ID != 0) glDeleteProgram(ID);
}

unsigned int Shader::compile(const char* src, GLenum type, const char* stageName, const char* path)
{
    unsigned int stage = glCreateShader(type);
    glShaderSource(stage, 1, &src, NULL);
    glCompileShader(stage);

    int success = GL_FALSE;
    glGetShaderiv(stage, GL_COMPILE_STATUS, &success);
    if (success == GL_TRUE) return stage;

    GLint logLength = 0;
    glGetShaderiv(stage, GL_INFO_LOG_LENGTH, &logLength);
    std::vector<char> infoLog(logLength > 1 ? static_cast<size_t>(logLength) : 1, '\0');
    glGetShaderInfoLog(stage, static_cast<GLsizei>(infoLog.size()), NULL, infoLog.data());

    std::cerr << "ERROR::SHADER::" << stageName << "::COMPILATION_FAILED\n"
        << " file : " << path << "\n"
        << infoLog.data() << std::endl;
    std::cerr << "--- source of " << path << " ---\n" << src << "\n--- end ---" << std::endl;

    glDeleteShader(stage);
    return 0;
}

std::string Shader::readFile(const char* path)
{
    std::ifstream file;
    file.exceptions(std::ifstream::failbit | std::ifstream::badbit);

    try {
        file.open(path);
        std::stringstream stream;
        stream << file.rdbuf();
        file.close();
        return stream.str();
    }
    catch (std::ifstream::failure&) {
        std::cerr << "ERROR::SHADER::FILE_NOT_SUCCESSFULLY_READ: " << path << std::endl;
        return "";
    }
}

bool Shader::checkLinkErrors(unsigned int program)
{
    int success = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (success == GL_TRUE) return true;

    GLint logLength = 0;
    glGetProgramiv(program, GL_INFO_LOG_LENGTH, &logLength);
    std::vector<char> infoLog(logLength > 1 ? static_cast<size_t>(logLength) : 1, '\0');
    glGetProgramInfoLog(program, static_cast<GLsizei>(infoLog.size()), NULL, infoLog.data());

    std::cerr << "ERROR::SHADER::PROGRAM::LINKING_FAILED\n" << infoLog.data() << std::endl;
    return false;
}