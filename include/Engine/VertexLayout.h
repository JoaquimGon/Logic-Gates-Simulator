#pragma once

#include <vector>
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <cstdint>

struct Attribute {
    int m_count;
    int m_offsetBytes;
};


/**
* @brief Utility class for configuring and building VAO attribute layouts.
*/
class VertexLayout {

private:
    std::vector<Attribute> attributes;
    int strideBytes = 0;

public:
    /**
    * @brief Registers a new vertex attribute layout specification for the VBO.
    * @param floatCount Number of floats for the attribute (e.g., 3 for x, y, z positions)
    */
    void addAttribute(int floatCount);
    
    /**
    * @brief Applies the vertex attributes to the VAO.
    */
    void applyToVAO() const;

    /**
    * @brief Returns stride in number of bytes.
    */
    int getStride() const;
};