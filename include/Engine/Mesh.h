#pragma once
#include "VertexLayout.h"

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <vector>

static class Mesh {
private:
    unsigned int VAO, VBO, EBO;
    int indexCount;
    int vertexCount = 0;
    bool usesEBO;
    unsigned int defaultDrawMode;
public:
    Mesh(const std::vector<float>& vertices,
        const std::vector<unsigned int>& indices,
        const VertexLayout& layout,
        unsigned int drawMode = GL_TRIANGLES);
    void draw() const;

};
