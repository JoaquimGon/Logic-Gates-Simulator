#pragma once

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <string>
#include <iostream>
#include <fstream>
#include <sstream>


class Shader
{
private:
    unsigned int compile(const char* src, GLenum type);
    std::string readFile(const char* path);
    void checkLinkErrors(unsigned int program);

public:
    unsigned int ID; // OpenGL program handle
    Shader(const char* vertexPath, const char* fragmentPath);
    void use() const;
    ~Shader();
    void setBool(const std::string& name, bool value) const;
    void setFloat(const std::string& name, float value) const;
    void setMat4(const std::string& name, const glm::mat4& mat) const;
};

