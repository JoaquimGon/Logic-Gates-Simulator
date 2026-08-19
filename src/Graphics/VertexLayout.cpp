#include "..\..\include\Graphics\VertexLayout.h"


void VertexLayout::addAttribute(int floatCount) {

    attributes.push_back({ floatCount, strideBytes });
    strideBytes += floatCount * sizeof(float);
}


void VertexLayout::applyToVAO() const {
    for (int i = 0; i < attributes.size(); i++) {
        const auto& attr = attributes[i];

        glVertexAttribPointer(
            i,                      // Attribute Slot
            attr.m_count,            // Size
            GL_FLOAT,               // Type
            GL_FALSE,               // Normalized
            strideBytes,            // STRIDE
            (void*)(intptr_t)attr.m_offsetBytes
        );
        glEnableVertexAttribArray(i);
    }
}


int VertexLayout::getStride() const { return strideBytes; }