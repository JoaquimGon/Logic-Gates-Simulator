#pragma once
#include "VertexLayout.h"

#include <glad/glad.h>
#include <vector>

/** @brief Owns geometry buffers and vertex-array state in the current OpenGL context. */
class Mesh
{
  private:
    unsigned int VAO = 0;
    unsigned int VBO = 0;
    unsigned int EBO = 0;
    int indexCount;
    int vertexCount = 0;
    bool usesEBO;
    unsigned int defaultDrawMode;

    unsigned int instanceVBO = 0;
    std::size_t m_vertexCapacity = 0, m_instanceCapacity = 0;
    std::vector<float> m_uploadedVertices, m_uploadedInstances;
    std::vector<int> m_instanceAttributes;
    int m_instanceAttributeStart = -1;

  public:
    Mesh(const Mesh&) = delete;
    Mesh& operator=(const Mesh&) = delete;

    /** @brief Upload interleaved vertices; an empty index list selects non-indexed drawing. */
    Mesh(
        const std::vector<float>& vertices, const std::vector<unsigned int>& indices, const VertexLayout& vertexLayout,
        unsigned int drawMode = GL_TRIANGLES
    );

    /** @brief Upload instance values and restore attribute bindings even when the layout and allocation are unchanged.
     */
    void setInstanceData(
        const std::vector<float>& instanceData, const std::vector<int>& attributeSizes, int startingAttributeLocation
    );

    void drawInstanced(int instanceCount) const;

    /** @brief Replace dynamic vertex data while retaining reusable buffer capacity and the correct draw count. */
    void updateData(const std::vector<float>& vertices, int floatsPerVertex);

    void draw() const;

    /**
     * @brief Releases the VAO, VBO, EBO and the instancing buffer.
     * Needs a current OpenGL context, and is safe to call more than once, so it
     * can be used both explicitly at shutdown and from the destructor.
     */
    void destroy();

    ~Mesh();
};
