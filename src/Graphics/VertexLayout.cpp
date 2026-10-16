#include "VertexLayout.h"

#include <cstdint>
#include <glad/glad.h>


void VertexLayout::addAttribute(int floatCount)
{
    attributes.push_back({floatCount, strideBytes});
    strideBytes += floatCount * sizeof(float);
}


void VertexLayout::applyToVAO() const
{
    for (int i = 0; i < attributes.size(); i++)
    {
        const auto& attr = attributes[i];

        glVertexAttribPointer(i, attr.m_count, GL_FLOAT, GL_FALSE, strideBytes, (void*)(intptr_t)attr.m_offsetBytes);
        glEnableVertexAttribArray(i);
    }
}


int VertexLayout::getStride() const
{
    return strideBytes;
}
