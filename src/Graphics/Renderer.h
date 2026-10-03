#pragma once
#include "Geometry/CanvasCamera.h"
#include "Graphics/Mesh.h"
#include "Graphics/Presentation/CanvasFrame.h"
#include "Graphics/Presentation/OverlayPresentation.h"
#include "Graphics/ShaderManager.h"
#include "Graphics/Text/TextPainter.h"

#include <memory>
#include <unordered_set>

/** Coordinates canvas layers and owns context-bound drawing resources. */
class Renderer
{
  public:
    Renderer();
    ~Renderer();
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;
    /** @return True when every required shader and the font were initialized. */
    bool init();
    /** @brief Idempotently releases GPU resources; requires their context to remain current. */
    void shutdown();
    void beginFrame(const CanvasCameraFrame& camera);
    void drawCanvas(const CanvasFrame& frame);
    void drawComponents(std::span<const ComponentRenderData> components);
    void drawPinLeads(std::span<const ComponentRenderData> components);
    void drawPins(
        std::span<const ComponentRenderData> components,
        int hoveredCompId = -1,
        int hoveredPinIdx = -1,
        PinType hoveredPinType = PinType::INPUT
    );
    void drawGrid();
    void drawWires(const std::map<WireId, Wire>& wires, const Wire* activeWire = nullptr);
    void drawWireSegmentBoundingBox(
        const GridCoords& start, const GridCoords& end, float padding = 0.03f, float alpha = 1
    );
    void drawComponentBoundingBox(
        glm::vec2 position, glm::vec2 size, float padding = 0.01f, float alpha = 1
    );
    void drawGridPointHighlight(GridCoords position, float opacity);
    void drawIntersections(std::span<const glm::vec3> intersections);
    /** @brief Submits generic world/screen text, reusable by later UI adapters. */
    void drawText(std::span<const TextRun> runs, TextSpace space);
    /** @brief Draws a filled rectangle in logical window pixels for basic UI. */
    void drawScreenRect(CanvasViewport bounds, glm::vec4 color);

    const FontMetrics& fontMetrics() const { return m_text.metrics(); }

    void drawLabels(std::span<const ComponentRenderData> components);
    void drawDebugOverlay(const DebugMetrics& metrics, bool showMetrics = true);

    int getDrawCallCount() const { return m_drawCallCount; }

  private:
    Shader* acquireShader(const std::string& name);
    ShaderManager m_sm;
    std::unordered_set<std::string> m_missingShaderWarned;
    std::unique_ptr<Mesh> m_gateMesh, m_gridMesh, m_pointMesh, m_wireMesh, m_boundsMesh;
    TextPainter m_text;
    CanvasCameraFrame m_currentCamera{};
    bool setCanvasViewport();
    bool useCanvasShader(Shader& shader);
    void setScreenViewport();
    int m_drawCallCount = 0;
};
