#pragma once

#include <vector>
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glad/glad.h>
#include <cstdint>

struct Attribute {
    int count;
    int offsetBytes;
};

class VertexLayout {
private:
    std::vector<Attribute> attributes;
    int strideBytes = 0;
public:
    void addAttribute(int floatCount);
    void applyToVAO() const;
    int getStride() const;
};