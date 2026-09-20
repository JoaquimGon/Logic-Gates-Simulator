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

    unsigned int instanceVBO = 0; // Holds the per-instance data

public:
    /**
     * @brief Initializes a Mesh object with geometry data.
     * @param vertices Array of vertex coordinates.
     * @param indices Array of index data for the EBO.
     * @param vertexLayout Vertex layout for the VAO.
     * @param drawMode Draw mode for openGL.
     */
    Mesh(const std::vector<float>& vertices,
        const std::vector<unsigned int>& indices,
        const VertexLayout& vertexLayout,
        unsigned int drawMode = GL_TRIANGLES);


    void setInstanceData(const std::vector<float>& instanceData, const std::vector<int>& attributeSizes, int startingAttributeLocation);

    void drawInstanced(int instanceCount) const;

    void updateData(const std::vector<float>& vertices, int floatsPerVertex);

    /**
    * @brief Draws the mesh to the screen.
    */
    void draw() const;
    
    /**
    * @brief Releases the VAO, VBO, EBO and the instancing buffer.
    * Needs a current OpenGL context, and is safe to call more than once, so it can
    * be used both explicitly at shutdown and from the destructor.
    */
    void destroy();

    /*
    * @brief Mesh deconstructor, releases the GPU buffers
    */
    ~Mesh();
};
