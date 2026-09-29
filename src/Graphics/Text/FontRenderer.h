#pragma once

#include "stb_truetype.h"

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <string>
#include <vector>

struct FontAtlas
{
    GLuint textureId = 0;
    stbtt_bakedchar cdata[96]; // ASCII 32..126
    float fontHeight = 32.0f;
    int atlasWidth = 512;
    int atlasHeight = 512;

    bool init(const std::string& ttfPath, float pixelHeight = 32.0f);
    void destroy();
};

struct TextVertex
{
    glm::vec2 pos;
    glm::vec2 uv;
    glm::vec4 color;
};

/**
 * @brief Appends a world-space triangle list for `text`.
 * @param x World-space x of the pen start. The glyph's left side bearing is part
 * of the quad, so the ink starts slightly to the right of it.
 * @param y World-space y of the baseline. Use getCapHeight() to centre text on a
 * row rather than guessing an offset.
 * @param worldScale World units per font pixel.
 * @param outVertices Appended to, so several strings can share one mesh.
 */
void buildTextGeometry(
    const std::string& text,
    float x,
    float y,
    float worldScale,
    const glm::vec4& color,
    const FontAtlas& font,
    std::vector<TextVertex>& outVertices
);

/**
 * @brief Width of `text` in world units, for centring and right-aligning.
 */
float getTextWidth(const std::string& text, float worldScale, const FontAtlas& font);

/**
 * @brief Height of a capital letter above the baseline, in world units.
 * The line box also contains ascent and descent padding, so centring on it would
 * push a row of capitals downwards.
 */
float getCapHeight(float worldScale, const FontAtlas& font);