#include "..\..\include\Engine\Shader.h"


Shader::Shader(const char* vertexPath, const char* fragmentPath) {
        std::string vCode = readFile(vertexPath);
        std::string fCode = readFile(fragmentPath);

        unsigned int vertexShader = compile(vCode.c_str(), GL_VERTEX_SHADER);
        unsigned int fragmentShader = compile(fCode.c_str(), GL_FRAGMENT_SHADER);

        ID = glCreateProgram();
        glAttachShader(ID, vertexShader);
        glAttachShader(ID, fragmentShader);
        glLinkProgram(ID);
        checkLinkErrors(ID);

        glDeleteShader(vertexShader);
        glDeleteShader(fragmentShader);
}


void Shader::use() const
{
    glUseProgram(ID);
}


// Set uniforms
void Shader::setBool(const std::string& name, bool value) const 
{
    glUniform1i(glGetUniformLocation(ID, name.c_str()), (int)value);
}
void Shader::setFloat(const std::string& name, float value) const 
{
    glUniform1f(glGetUniformLocation(ID, name.c_str()), value);
}
void Shader::setMat3(const std::string& name, const glm::mat3& mat) const
{
    glUniformMatrix4fv(glGetUniformLocation(ID, name.c_str()), 1, GL_FALSE, &mat[0][0]);
}
void Shader::setMat4(const std::string& name, const glm::mat4& mat) const 
{
    glUniformMatrix4fv(glGetUniformLocation(ID, name.c_str()), 1, GL_FALSE, &mat[0][0]);
}
void Shader::setVec2(const std::string& name, float value1, float value2) const
{
    glUniform2f(glGetUniformLocation(ID, name.c_str()), value1, value2);
}


Shader::~Shader() { glDeleteProgram(ID); }


unsigned int Shader::compile(const char* src, GLenum type)
{
        unsigned int vertexShader = glCreateShader(type);
        
        glShaderSource(vertexShader, 1, &src, NULL);
        glCompileShader(vertexShader);

        int success;
        char infoLog[512];

        glGetShaderiv(vertexShader, GL_COMPILE_STATUS, &success);
        if (!success)
        {
            glGetShaderInfoLog(vertexShader, 512, NULL, infoLog);
            std::cout << "ERROR::SHADER::VERTEX::COMPILATION_FAILED\n" << infoLog << std::endl;
            std::cout << "Shader " << ID << ", code: \n" << src << "\n";
        }

        return vertexShader;
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


void Shader::checkLinkErrors(unsigned int program)
{
    int success;
    char infoLog[512];

    // check for linking errors
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success) {
        glGetProgramInfoLog(program, 512, NULL, infoLog);
        std::cout << "ERROR::SHADER::PROGRAM::LINKING_FAILED\n" << infoLog << std::endl;
    }
}


