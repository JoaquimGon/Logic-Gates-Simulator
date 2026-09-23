#include "Shader.h"
#ifndef PROJECT_ASSETS_DIR
#define PROJECT_ASSETS_DIR "assets"
#endif
#include <vector>
#include <filesystem>

Shader::Shader(const char* vertexPath, const char* fragmentPath)
{
    // Convert relative paths to guaranteed absolute paths on the host machine
    std::string vResolved = resolvePath(vertexPath);
    std::string fResolved = resolvePath(fragmentPath);

    std::string vCode = readFile(vResolved.c_str());
    std::string fCode = readFile(fResolved.c_str());

    if (vCode.empty() || fCode.empty()) {
        std::cerr << "ERROR::SHADER::BUILD_SKIPPED: missing source for \""
            << vResolved << "\" / \"" << fResolved << "\".\n";
        return; // ID stays 0, so isValid() is false
    }

    unsigned int vertexShader = compile(vCode.c_str(), GL_VERTEX_SHADER, "VERTEX", vResolved.c_str());
    unsigned int fragmentShader = compile(fCode.c_str(), GL_FRAGMENT_SHADER, "FRAGMENT", fResolved.c_str());

    if (vertexShader == 0 || fragmentShader == 0) {
        std::cerr << "ERROR::SHADER::BUILD_FAILED: \"" << vResolved << "\" / \""
            << fResolved << "\" will not be usable.\n";
        if (vertexShader != 0) glDeleteShader(vertexShader);
        if (fragmentShader != 0) glDeleteShader(fragmentShader);
        return; // ID stays 0
    }

    ID = glCreateProgram();
    glAttachShader(ID, vertexShader);
    glAttachShader(ID, fragmentShader);
    glLinkProgram(ID);

    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    if (!checkLinkErrors(ID)) {
        glDeleteProgram(ID);
        ID = 0;
    }
}


void Shader::use() const
{
    if (ID == 0) return; // invalid shader: the constructor already reported why
    glUseProgram(ID);
}

int Shader::uniformLocation(const std::string& name) const
{
    // Program 0 is not a valid program object, so never query it: glUniform*() with
    // a location of -1 is a documented no-op, which keeps every setter below safe to
    // call even when the shader failed to build. A uniform that does not exist also
    // resolves to -1, so a typo cannot raise a GL error either.
    return (ID == 0) ? -1 : glGetUniformLocation(ID, name.c_str());
}


// Set uniforms
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
    // glm::mat3 is column-major and tightly packed, so &mat[0][0] is exactly the
    // 9-float buffer glUniformMatrix3fv expects. This used to call the 4x4 variant,
    // which read 16 floats out of a 9-float matrix.
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


Shader::~Shader() { if (ID != 0) glDeleteProgram(ID); }


unsigned int Shader::compile(const char* src, GLenum type, const char* stageName, const char* path)
{
        unsigned int stage = glCreateShader(type);
        
        glShaderSource(stage, 1, &src, NULL);
        glCompileShader(stage);

        int success = GL_FALSE;
        glGetShaderiv(stage, GL_COMPILE_STATUS, &success);
        if (success == GL_TRUE) return stage;

        // Report which stage of which file failed - this used to label fragment
        // errors as "VERTEX" and print an uninitialised program id - then dump the
        // source so the info log's line numbers can be matched up.
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
    catch (std::ifstream::failure& e) {
        std::cerr << "ERROR::SHADER::FILE_NOT_SUCCESSFULLY_READ: " << path << std::endl;
        return "";
    }
}


bool Shader::checkLinkErrors(unsigned int program)
{
    int success = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (success == GL_TRUE) return true;

    // Sized from GL_INFO_LOG_LENGTH instead of a fixed buffer, so a long linker
    // message is never silently truncated.
    GLint logLength = 0;
    glGetProgramiv(program, GL_INFO_LOG_LENGTH, &logLength);
    std::vector<char> infoLog(logLength > 1 ? static_cast<size_t>(logLength) : 1, '\0');
    glGetProgramInfoLog(program, static_cast<GLsizei>(infoLog.size()), NULL, infoLog.data());

    std::cerr << "ERROR::SHADER::PROGRAM::LINKING_FAILED\n" << infoLog.data() << std::endl;
    return false;
}


