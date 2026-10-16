#define STB_TRUETYPE_IMPLEMENTATION
#include "Graphics/Text/TextGeometry.h"


float getTextWidth(const std::string& text, float worldScale, const FontMetrics& font)
{
    float curX = 0.0f;
    float curY = 0.0f;
    for (char c : text)
    {
        if (c < 32 || c >= 128)
            continue;

        stbtt_aligned_quad q;
        stbtt_GetBakedQuad(font.cdata, font.atlasWidth, font.atlasHeight, c - 32, &curX, &curY, &q, 1);
    }
    return curX * worldScale;
}


float getCapHeight(float worldScale, const FontMetrics& font)
{
    // The baked H glyph measures capital height; stb reports its top above the baseline as negative Y.
    float penX = 0.0f;
    float penY = 0.0f;
    stbtt_aligned_quad q;
    stbtt_GetBakedQuad(font.cdata, font.atlasWidth, font.atlasHeight, 'H' - 32, &penX, &penY, &q, 1);
    return -q.y0 * worldScale;
}


void buildTextGeometry(
    const std::string& text, float x, float y, float worldScale, const glm::vec4& color, const FontMetrics& font,
    std::vector<TextVertex>& outVertices, TextSpace space
)
{
    if (worldScale <= 0.0f)
        return;

    // Convert the world origin to font pixels once; applying worldScale to it twice displaces labels.
    float curX = x / worldScale;
    float curY = y / worldScale; // baseline in pixels; stb only advances the x component

    for (char c : text)
    {
        if (c < 32 || c >= 128)
            continue;

        stbtt_aligned_quad q;
        stbtt_GetBakedQuad(font.cdata, font.atlasWidth, font.atlasHeight, c - 32, &curX, &curY, &q, 1);

        const float x0 = q.x0 * worldScale;
        const float x1 = q.x1 * worldScale;

        // Mirror glyph corners around the pen baseline to convert stb's downward Y into world-space Y.
        const float sign = space == TextSpace::World ? -1.0f : 1.0f;
        const float y0 = y + sign * (q.y0 - curY) * worldScale;
        const float y1 = y + sign * (q.y1 - curY) * worldScale;

        outVertices.push_back({{x0, y0}, {q.s0, q.t0}, color});
        outVertices.push_back({{x1, y0}, {q.s1, q.t0}, color});
        outVertices.push_back({{x1, y1}, {q.s1, q.t1}, color});

        outVertices.push_back({{x0, y0}, {q.s0, q.t0}, color});
        outVertices.push_back({{x1, y1}, {q.s1, q.t1}, color});
        outVertices.push_back({{x0, y1}, {q.s0, q.t1}, color});
    }
}


std::vector<float> packTextVertices(const std::vector<TextVertex>& vertices)
{
    std::vector<float> data;
    data.reserve(vertices.size() * 8);
    for (const auto& v : vertices)
        data.insert(data.end(), {v.pos.x, v.pos.y, v.uv.x, v.uv.y, v.color.r, v.color.g, v.color.b, v.color.a});
    return data;
}
