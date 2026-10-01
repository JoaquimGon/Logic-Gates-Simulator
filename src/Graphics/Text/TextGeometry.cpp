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
        stbtt_GetBakedQuad(
            font.cdata, font.atlasWidth, font.atlasHeight, c - 32, &curX, &curY, &q, 1
        );
    }
    return curX * worldScale;
}

float getCapHeight(float worldScale, const FontMetrics& font)
{
    // 'H' is a plain capital with flat top and bottom, so its box above the
    // baseline is exactly the cap height. stb measures y downwards, hence the
    // negation.
    float penX = 0.0f;
    float penY = 0.0f;
    stbtt_aligned_quad q;
    stbtt_GetBakedQuad(
        font.cdata, font.atlasWidth, font.atlasHeight, 'H' - 32, &penX, &penY, &q, 1
    );
    return -q.y0 * worldScale;
}

void buildTextGeometry(
    const std::string& text,
    float x,
    float y,
    float worldScale,
    const glm::vec4& color,
    const FontMetrics& font,
    std::vector<TextVertex>& outVertices,
    TextSpace space
)
{
    if (worldScale <= 0.0f)
        return;

    // stb advances the pen in font pixel space and reports absolute pixel corners,
    // so the world-space origin is converted to pixels once, here. Seeding the pen
    // with world coordinates would apply worldScale to them a second time and
    // strand the text next to the world origin, barely moving when the component
    // it belongs to does.
    float curX = x / worldScale;
    float curY = y / worldScale; // baseline in pixels; stb only advances the x component

    for (char c : text)
    {
        if (c < 32 || c >= 128)
            continue;

        stbtt_aligned_quad q;
        stbtt_GetBakedQuad(
            font.cdata, font.atlasWidth, font.atlasHeight, c - 32, &curX, &curY, &q, 1
        );

        const float x0 = q.x0 * worldScale;
        const float x1 = q.x1 * worldScale;

        // stb measures y downwards while the scene is y-up, so each corner is
        // mirrored about the baseline: q.y0 is the glyph's top edge, q.y1 its
        // bottom. curY is the pen baseline the offsets are relative to.
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
        data.insert(
            data.end(),
            {v.pos.x, v.pos.y, v.uv.x, v.uv.y, v.color.r, v.color.g, v.color.b, v.color.a}
        );
    return data;
}
