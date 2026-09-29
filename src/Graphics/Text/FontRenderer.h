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

void buildTextGeometry(
    const std::string& text,
    float x,
    float y,
    float worldScale,
    const glm::vec4& color,
    const FontAtlas& font,
    std::vector<TextVertex>& outVertices
);

float getTextWidth(const std::string& text, float worldScale, const FontAtlas& font);