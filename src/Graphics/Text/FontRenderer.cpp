#include "FontRenderer.h"

#include <fstream>
#include <glad/glad.h>
#include <iostream>

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

bool FontAtlas::init(const std::string& ttfPath, float pixelHeight)
{
    fontHeight = pixelHeight;

    std::ifstream file(ttfPath, std::ios::binary | std::ios::ate);
    if (!file.is_open())
    {
        std::cerr << "[FontAtlas] Failed to open font: " << ttfPath << std::endl;
        return false;
    }
    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);
    std::vector<unsigned char> ttfBuffer(size);
    file.read(reinterpret_cast<char*>(ttfBuffer.data()), size);

    std::vector<unsigned char> alphaBitmap(atlasWidth * atlasHeight);
    int result = stbtt_BakeFontBitmap(
        ttfBuffer.data(), 0, fontHeight, alphaBitmap.data(), atlasWidth, atlasHeight, 32, 96, cdata
    );

    if (result <= 0)
    {
        std::cerr << "[FontAtlas] Warning: Font atlas too small for all glyphs!\n";
    }

    glGenTextures(1, &textureId);
    glBindTexture(GL_TEXTURE_2D, textureId);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        GL_RED,
        atlasWidth,
        atlasHeight,
        0,
        GL_RED,
        GL_UNSIGNED_BYTE,
        alphaBitmap.data()
    );

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    return true;
}

void FontAtlas::destroy()
{
    if (textureId != 0)
    {
        glDeleteTextures(1, &textureId);
        textureId = 0;
    }
}

float getTextWidth(const std::string& text, float worldScale, const FontAtlas& font)
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


float getCapHeight(float worldScale, const FontAtlas& font)
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
    const FontAtlas& font,
    std::vector<TextVertex>& outVertices
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
        const float y0 = y - (q.y0 - curY) * worldScale;
        const float y1 = y - (q.y1 - curY) * worldScale;

        outVertices.push_back({{x0, y0}, {q.s0, q.t0}, color});
        outVertices.push_back({{x1, y0}, {q.s1, q.t0}, color});
        outVertices.push_back({{x1, y1}, {q.s1, q.t1}, color});

        outVertices.push_back({{x0, y0}, {q.s0, q.t0}, color});
        outVertices.push_back({{x1, y1}, {q.s1, q.t1}, color});
        outVertices.push_back({{x0, y1}, {q.s0, q.t1}, color});
    }
}