#pragma once
#include "Graphics/Text/TextGeometry.h"

#include <glad/glad.h>

/** @brief GPU atlas ownership; release while its OpenGL context is current. */
struct FontAtlas : FontMetrics
{
    GLuint textureId = 0;

    FontAtlas() = default;
    FontAtlas(const FontAtlas&) = delete;
    FontAtlas& operator=(const FontAtlas&) = delete;
    ~FontAtlas() { destroy(); }

    /** @brief Load and bake the configured font into a GPU atlas while retaining glyph metrics for layout. */
    bool init(const std::string& ttfPath, float pixelHeight = 32.0f);

    void destroy();
};
