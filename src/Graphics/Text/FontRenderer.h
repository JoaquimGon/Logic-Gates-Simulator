#pragma once
#include "Graphics/Text/TextGeometry.h"

#include <glad/glad.h>

/** GPU atlas ownership; release while its OpenGL context is current. */
struct FontAtlas : FontMetrics
{
    GLuint textureId = 0;
    FontAtlas() = default;
    FontAtlas(const FontAtlas&) = delete;
    FontAtlas& operator=(const FontAtlas&) = delete;

    ~FontAtlas() { destroy(); }
    bool init(const std::string& ttfPath, float pixelHeight = 32.0f);
    void destroy();
};
