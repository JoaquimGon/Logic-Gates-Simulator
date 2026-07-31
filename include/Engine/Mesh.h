#pragma once
#include "VertexLayout.h"

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <vector>

/**
* @brief Wrapper class for VAO, VBO and EBO creation and binding
*/
class Mesh {

private:
    unsigned int VAO = 0;
    unsigned int VBO = 0;
    unsigned int EBO = 0;
    int indexCount;
    int vertexCount = 0;
    bool usesEBO;
    unsigned int defaultDrawMode;
public:
    /**
     * @brief Initializes a Mesh object with geometry data.
     * @param vertices Array of vertex coordinates.
     * @param indices Array of index data for the EBO.
     * @param layout Vertex layout for the VAO.
     * @param drawMode Draw mode for openGL.
     */
    Mesh(const std::vector<float>& vertices,
        const std::vector<unsigned int>& indices,
        const VertexLayout& layout,
        unsigned int drawMode = GL_TRIANGLES);

    /**
    * @brief Draws the mesh to the screen.
    */
    void draw() const;

};
