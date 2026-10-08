#include "Mesh.h"

#include <algorithm>

namespace
{
// The bound buffer keeps its storage until the incoming data outgrows it.
void upload(const std::vector<float>& data, std::vector<float>& uploaded, std::size_t& capacity)
{
    if (data == uploaded)
        return;
    const auto bytes = data.size() * sizeof(float);
    if (bytes > capacity)
    {
        capacity = std::max(bytes, capacity * 2);
        glBufferData(GL_ARRAY_BUFFER, capacity, nullptr, GL_DYNAMIC_DRAW);
    }
    if (bytes)
        glBufferSubData(GL_ARRAY_BUFFER, 0, bytes, data.data());
    uploaded = data;
}
} // namespace

Mesh::Mesh(
    const std::vector<float>& vertices,
    const std::vector<unsigned int>& indices,
    const VertexLayout& vertexLayout,
    unsigned int drawMode
)
{
    defaultDrawMode = drawMode;
    usesEBO = !indices.empty();
    indexCount = static_cast<int>(indices.size());

    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);
    glBindVertexArray(VAO);

    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), vertices.data(), GL_STATIC_DRAW);
    m_vertexCapacity = vertices.size() * sizeof(float);
    m_uploadedVertices = vertices;

    if (usesEBO)
    {
        glGenBuffers(1, &EBO);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
        glBufferData(
            GL_ELEMENT_ARRAY_BUFFER,
            indices.size() * sizeof(unsigned int),
            indices.data(),
            GL_STATIC_DRAW
        );
    }
    else
    {
        vertexCount =
            static_cast<int>(vertices.size() / (vertexLayout.getStride() / sizeof(float)));
    }

    vertexLayout.applyToVAO();

    glBindVertexArray(0);
}

Mesh::~Mesh()
{
    destroy();
}

void Mesh::destroy()
{
    // glDelete*() silently ignores 0, but zeroing the handles afterwards both
    // keeps this idempotent and makes it obvious the mesh no longer owns any GL
    // object.
    if (VAO != 0)
        glDeleteVertexArrays(1, &VAO);
    if (VBO != 0)
        glDeleteBuffers(1, &VBO);
    if (EBO != 0)
        glDeleteBuffers(1, &EBO);
    if (instanceVBO != 0)
        glDeleteBuffers(1, &instanceVBO); // this one used to be leaked

    VAO = 0;
    VBO = 0;
    EBO = 0;
    instanceVBO = 0;
    m_vertexCapacity = m_instanceCapacity = 0;
    m_uploadedVertices.clear();
    m_uploadedInstances.clear();
    m_instanceAttributes.clear();
    m_instanceAttributeStart = -1;
}

void Mesh::draw() const
{
    glBindVertexArray(VAO);
    if (usesEBO)
    {
        glDrawElements(defaultDrawMode, indexCount, GL_UNSIGNED_INT, 0);
    }
    else
    {
        glDrawArrays(defaultDrawMode, 0, vertexCount);
    }
}

void Mesh::setInstanceData(
    const std::vector<float>& instanceData,
    const std::vector<int>& attributeSizes,
    int startingAttributeLocation
)
{
    glBindVertexArray(VAO);

    if (instanceVBO == 0)
    {
        glGenBuffers(1, &instanceVBO);
    }

    glBindBuffer(GL_ARRAY_BUFFER, instanceVBO);
    upload(instanceData, m_uploadedInstances, m_instanceCapacity);
    if (m_instanceAttributes == attributeSizes &&
        m_instanceAttributeStart == startingAttributeLocation)
    {
        glBindVertexArray(0);
        return;
    }
    for (std::size_t i = 0; i < m_instanceAttributes.size(); ++i)
        glDisableVertexAttribArray(m_instanceAttributeStart + static_cast<int>(i));
    m_instanceAttributes = attributeSizes;
    m_instanceAttributeStart = startingAttributeLocation;

    // Calculate total stride (e.g., 2 + 4 = 6 floats total per instance)
    int totalStride = 0;
    for (int size : attributeSizes)
    {
        totalStride += size;
    }

    // Set up the attribute pointers
    int currentOffset = 0;
    for (size_t i = 0; i < attributeSizes.size(); ++i)
    {
        int location = startingAttributeLocation + static_cast<int>(i);
        glEnableVertexAttribArray(location);
        glVertexAttribPointer(
            location,
            attributeSizes[i],
            GL_FLOAT,
            GL_FALSE,
            totalStride * sizeof(float),
            (void*)(currentOffset * sizeof(float))
        );
        glVertexAttribDivisor(location, 1); // Tell OpenGL this advances PER INSTANCE

        currentOffset += attributeSizes[i];
    }

    glBindVertexArray(0);
}

void Mesh::drawInstanced(int instanceCount) const
{
    glBindVertexArray(VAO);
    if (usesEBO)
    {
        glDrawElementsInstanced(defaultDrawMode, indexCount, GL_UNSIGNED_INT, 0, instanceCount);
    }
    else
    {
        glDrawArraysInstanced(defaultDrawMode, 0, vertexCount, instanceCount);
    }
    glBindVertexArray(0);
}

void Mesh::updateData(const std::vector<float>& vertices, int floatsPerVertex)
{
    glBindVertexArray(VAO);
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    upload(vertices, m_uploadedVertices, m_vertexCapacity);

    // Update the vertex count so glDrawArrays knows how many points to draw
    vertexCount = static_cast<int>(vertices.size() / floatsPerVertex);

    glBindVertexArray(0);
}
