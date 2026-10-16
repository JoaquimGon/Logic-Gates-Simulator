#pragma once

#include <vector>

struct Attribute
{
    int m_count;
    int m_offsetBytes;
};

/** @brief Describes interleaved float attributes for a vertex array. */
class VertexLayout
{
  private:
    std::vector<Attribute> attributes;
    int strideBytes = 0;

  public:
    void addAttribute(int floatCount);

    /** @brief Configure attributes on the currently bound vertex array. */
    void applyToVAO() const;

    /** @return Stride in bytes. */
    int getStride() const;
};
