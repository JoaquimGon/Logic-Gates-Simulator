#include "FontRenderer.h"

#include <fstream>
#include <glad/glad.h>
#include <iostream>


bool FontAtlas::init(const std::string& ttfPath, float pixelHeight)
{
    destroy();
    fontHeight = pixelHeight;

    std::ifstream file(ttfPath, std::ios::binary | std::ios::ate);
    if (!file.is_open())
    {
        std::cerr << "[FontAtlas] Failed to open font: " << ttfPath << std::endl;
        return false;
    }
    std::streamsize size = file.tellg();
    if (size <= 0)
        return false;
    file.seekg(0, std::ios::beg);
    std::vector<unsigned char> ttfBuffer(size);
    if (!file.read(reinterpret_cast<char*>(ttfBuffer.data()), size))
        return false;

    std::vector<unsigned char> alphaBitmap(atlasWidth * atlasHeight);
    int result = stbtt_BakeFontBitmap(
        ttfBuffer.data(), 0, fontHeight, alphaBitmap.data(), atlasWidth, atlasHeight, 32, 96, cdata
    );

    if (result <= 0)
    {
        std::cerr << "[FontAtlas] Font atlas does not contain all required glyphs.\n";
        return false;
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
