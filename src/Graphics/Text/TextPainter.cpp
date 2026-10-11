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
    if (!prepareMesh(*m_mesh, runs, space))
        return 0;
    return drawPrepared(*m_mesh, projection, shader);
}

bool TextPainter::prepareMesh(Mesh& mesh, std::span<const TextRun> runs, TextSpace space)
{
    if (!m_font.textureId)
        return false;
    auto& vertices = m_vertices;
    vertices.clear();
    for (const auto& run : runs)
        buildTextGeometry(
            run.text, run.baseline.x, run.baseline.y, run.scale, run.color, m_font, vertices, space
        );
    if (vertices.empty())
        return false;
    mesh.updateData(packTextVertices(vertices), 8);
    return true;
}

int TextPainter::drawPrepared(const Mesh& mesh, const glm::mat4& projection, Shader& shader)
{
    if (!m_font.textureId || !shader.isValid())
        return 0;
    shader.use();
    shader.setMat4("uProjectionView", projection);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_font.textureId);
    shader.setBool("uFontTexture", false);
    mesh.draw();
    return 1;
}
