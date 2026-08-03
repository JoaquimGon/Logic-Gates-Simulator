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

