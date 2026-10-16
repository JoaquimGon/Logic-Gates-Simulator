#pragma once
#include "Graphics/Mesh.h"
#include "Graphics/Shader.h"
#include "Graphics/Text/FontRenderer.h"

#include <memory>
#include <span>

/** @brief Owns GPU text resources; submission accepts already laid-out world or screen text. */
class TextPainter
{
  public:
    /** @brief Load the text shader and font atlas, releasing incomplete resources if either initialization fails. */
    bool init(const std::string& path);

    void shutdown();

    const FontMetrics& metrics() const { return m_font; }

    /** @return Number of actual draw calls (zero for empty/uninitialized submission). */
    int draw(std::span<const TextRun> runs, TextSpace space, const glm::mat4& projection, Shader& shader);

    /** @brief Builds/uploads a retained batch using the existing font; false for empty text. */
    bool prepareMesh(Mesh& mesh, std::span<const TextRun> runs, TextSpace space);

    /** @brief Draws a prepared nonempty mesh without rebuilding or uploading glyphs. */
    int drawPrepared(const Mesh& mesh, const glm::mat4& projection, Shader& shader);

  private:
    FontAtlas m_font;
    std::unique_ptr<Mesh> m_mesh;
    std::vector<TextVertex> m_vertices;
};
