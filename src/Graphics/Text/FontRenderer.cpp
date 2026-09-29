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
    float curX = x;
    float curY = y;

    for (char c : text)
    {
        if (c < 32 || c >= 128)
            continue;

        stbtt_aligned_quad q;
        stbtt_GetBakedQuad(
            font.cdata, font.atlasWidth, font.atlasHeight, c - 32, &curX, &curY, &q, 1
        );

        float x0 = q.x0 * worldScale;
        float x1 = q.x1 * worldScale;
        float y0 = q.y0 * worldScale;
        float y1 = q.y1 * worldScale;

        float h = y1 - y0;
        y0 = y - (y0 - y);
        y1 = y0 - h;

        outVertices.push_back({{x0, y0}, {q.s0, q.t0}, color});
        outVertices.push_back({{x1, y0}, {q.s1, q.t0}, color});
        outVertices.push_back({{x1, y1}, {q.s1, q.t1}, color});

        outVertices.push_back({{x0, y0}, {q.s0, q.t0}, color});
        outVertices.push_back({{x1, y1}, {q.s1, q.t1}, color});
        outVertices.push_back({{x0, y1}, {q.s0, q.t1}, color});
    }
}