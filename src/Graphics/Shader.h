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
    /*
    * @brief Compiles one shader stage and reports any compiler diagnostics.
    * @param src Source code of the stage.
    * @param type OpenGL stage type (GL_VERTEX_SHADER, GL_FRAGMENT_SHADER, ...).
    * @param stageName Stage tag used in error messages, e.g. "VERTEX".
    * @param path File the source was read from, used in error messages.
    * @return The compiled stage object, or 0 when compilation failed.
    */
    unsigned int compile(const char* src, GLenum type, const char* stageName, const char* path);

    /*
    * @brief Reads a shader source file.
    * @param path File path of the shader source.
    * @return The file contents, or an empty string when it could not be read.
    */
    std::string readFile(const char* path);

    /*
    * @brief Reports whether glLinkProgram succeeded, printing the info log when it did not.
    * @param program The linked program to inspect.
    * @return true when the program linked successfully.
    */
    bool checkLinkErrors(unsigned int program);

    /*
    * @brief Resolves a uniform location, tolerating an unusable program.
    * @param name The name of the uniform as written in the GLSL source code.
    * @return The uniform location, or -1 when the program is invalid or the uniform
    *         does not exist. glUniform*() treats -1 as a no-op, so callers can pass
    *         it straight through.
    */
    int uniformLocation(const std::string& name) const;

    unsigned int ID = 0; // OpenGL program handle; 0 means the shader is unusable

public:
    /**
    * @brief Shader initializer, loads file and compiles shader
    * @param vertexPath File path for the vertex shader
    * @param fragmentPath File path for the fragment shader
    */
    Shader(const char* vertexPath, const char* fragmentPath);

    /*
    * @brief Points to openGL to use this shader. Does nothing when the shader
    * could not be built (see isValid()).
    */
    void use() const;

    /*
    * @brief Whether this shader compiled and linked successfully.
    * @return false when the shader is unusable, so callers can report it and skip
    *         drawing instead of rendering nothing with no explanation.
    */
    bool isValid() const { return ID != 0; }

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
    * @param mat The glm::mat3 matrix to be passed to the shader
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
    * @paral valueX Value of each item in the vector
    */
    void setVec2(const std::string& name, float value1, float value2) const;


    /*
    * @brief Sets a shader's vector of size 4 uniform
    * @param name Name of the variable in the shader
    * @paral valueX Value of each item in the vector
    */
    void setVec4(const std::string& name, float value1, float value2, float value3, float value4) const;
};

