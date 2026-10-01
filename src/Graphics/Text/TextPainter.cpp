#include "Graphics/Text/TextPainter.h"

bool TextPainter::init(const std::string& path)
{
    shutdown();
    if (!m_font.init(path, 48))
        return false;
    VertexLayout layout;
    layout.addAttribute(2);
    layout.addAttribute(2);
    layout.addAttribute(4);
    m_mesh = std::make_unique<Mesh>(
        std::vector<float>{}, std::vector<unsigned int>{}, layout, GL_TRIANGLES
    );
    return true;
}

void TextPainter::shutdown()
{
    m_mesh.reset();
    m_font.destroy();
}

int TextPainter::draw(
    std::span<const TextRun> runs, TextSpace space, const glm::mat4& projection, Shader& shader
)
{
    if (!m_mesh || !m_font.textureId || !shader.isValid())
        return 0;
    std::vector<TextVertex> vertices;
    for (const auto& run : runs)
        buildTextGeometry(
            run.text, run.baseline.x, run.baseline.y, run.scale, run.color, m_font, vertices, space
        );
    if (vertices.empty())
        return 0;
    shader.use();
    shader.setMat4("uProjectionView", projection);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_font.textureId);
    shader.setBool("uFontTexture", false);
    m_mesh->updateData(packTextVertices(vertices), 8);
    m_mesh->draw();
    return 1;
}
