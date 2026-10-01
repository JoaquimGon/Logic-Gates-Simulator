#pragma once
#include "Graphics/Mesh.h"
#include "Graphics/Shader.h"
#include "Graphics/Text/FontRenderer.h"

#include <memory>
#include <span>

/** Owns GPU text resources; submission accepts already laid-out world or screen text. */
class TextPainter
{
  public:
    bool init(const std::string& path);
    void shutdown();

    const FontMetrics& metrics() const { return m_font; }

    /** @return Number of actual draw calls (zero for empty/uninitialized submission). */
    int draw(
        std::span<const TextRun> runs, TextSpace space, const glm::mat4& projection, Shader& shader
    );

  private:
    FontAtlas m_font;
    std::unique_ptr<Mesh> m_mesh;
};
