#include "..\..\include\Engine\Mesh.h"


Mesh::Mesh(const std::vector<float>& vertices,
        const std::vector<unsigned int>& indices,
        const VertexLayout& vertexLayout,
        unsigned int drawMode)
    {
        defaultDrawMode = drawMode;
        usesEBO = !indices.empty();
        indexCount = indices.size();


        glGenVertexArrays(1, &VAO);
        glGenBuffers(1, &VBO);
        glBindVertexArray(VAO);

        glBindBuffer(GL_ARRAY_BUFFER, VBO);
        glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), vertices.data(), GL_STATIC_DRAW);

        if (usesEBO) {
            glGenBuffers(1, &EBO);
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
            glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int), indices.data(), GL_STATIC_DRAW);
        }
        else {
            vertexCount = vertices.size() / (vertexLayout.getStride() / sizeof(float));
        }

        vertexLayout.applyToVAO();

        glBindVertexArray(0);
    }


Mesh::~Mesh()
{
    glDeleteVertexArrays(1, &VAO);
    glDeleteBuffers(1, &VBO);
    glDeleteBuffers(1, &EBO);
}

void Mesh::draw() const {
        glBindVertexArray(VAO);
        if (usesEBO) {
            glDrawElements(defaultDrawMode, indexCount, GL_UNSIGNED_INT, 0);
        }
        else {
            glDrawArrays(defaultDrawMode, 0, vertexCount);
        }
}


void Mesh::setInstanceData(const std::vector<float>& instanceData, const std::vector<int>& attributeSizes, int startingAttributeLocation)
{
    glBindVertexArray(VAO);

    if (instanceVBO == 0) {
        glGenBuffers(1, &instanceVBO);
    }

    glBindBuffer(GL_ARRAY_BUFFER, instanceVBO);
    glBufferData(GL_ARRAY_BUFFER, instanceData.size() * sizeof(float), instanceData.data(), GL_DYNAMIC_DRAW);

    // Calculate total stride (e.g., 2 + 4 = 6 floats total per instance)
    int totalStride = 0;
    for (int size : attributeSizes) {
        totalStride += size;
    }

    // Set up the attribute pointers
    int currentOffset = 0;
    for (size_t i = 0; i < attributeSizes.size(); ++i) {
        int location = startingAttributeLocation + i;
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
    if (usesEBO) {
        glDrawElementsInstanced(defaultDrawMode, indexCount, GL_UNSIGNED_INT, 0, instanceCount);
    }
    else {
        glDrawArraysInstanced(defaultDrawMode, 0, vertexCount, instanceCount);
    }
    glBindVertexArray(0);
}