#pragma once

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <string>
#include <iostream>
#include <fstream>
#include <sstream>

/*
* @brief Class that handles shaders files, compilation and linking.
*/
class Shader
{
private:
    unsigned int compile(const char* src, GLenum type);
    std::string readFile(const char* path);
    void checkLinkErrors(unsigned int program);
    unsigned int ID; // OpenGL program handle

public:
    /**
    * @brief Shader initializer, loads file and compiles shader
    * @param vertexPath File path for the vertex shader
    * @param fragmentPath File path for the fragment shader
    */
    Shader(const char* vertexPath, const char* fragmentPath);

    /*
    * @brief Points to openGL to use this shader
    */
    void use() const;

    /*
    * @brief Shader class deconstructor
    */
    ~Shader();

    /*
    * @brief Sets a boolean uniform variable in the shader.
    * @param name The name of the uniform as written in the GLSL source code.
    * @param value The boolean value to assign (true or false).
    */
    void setBool(const std::string& name, bool value) const;
    
    /*
    * @brief Sets a float uniform variable in the shader.
    * @param name The name of the uniform as written in the GLSL source code.
    * @param value The float value to assign.
    */
    void setFloat(const std::string& name, float value) const;
    
    /*
    * @brief Sets a shader's 3x3 matrix uniform
    * @param name Name of the variable in the shader
    * @param mat The glm::mat4 matrix to be passed to the shader
    */
    void setMat3(const std::string& name, const glm::mat3& mat) const;

    /*
    * @brief Sets a shader's 4x4 matrix uniform
    * @param name Name of the variable in the shader
    * @param mat The glm::mat4 matrix to be passed to the shader
    */
    void setMat4(const std::string& name, const glm::mat4& mat) const;


    /*
    * @brief Sets a shader's vector of size 2 uniform
    * @param name Name of the variable in the shader
    * @param mat The glm::mat4 matrix to be passed to the shader
    */
    void setVec2(const std::string& name, float value1, float value2) const;
};

