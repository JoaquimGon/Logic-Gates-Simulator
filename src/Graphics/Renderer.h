#pragma once
#include "Geometry/CanvasCamera.h"
#include "Graphics/Mesh.h"
#include "Graphics/Presentation/CanvasFrame.h"
#include "Graphics/Presentation/OverlayPresentation.h"
#include "Graphics/ShaderManager.h"
#include "Graphics/Text/TextPainter.h"

#include <chrono>
#include <memory>
#include <optional>
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
    void drawComponentBoundingBox(BodyBounds bounds, float padding = 0.01f, float alpha = 1);
    void drawWorldRect(BodyBounds bounds, glm::vec4 color);
    void drawGridPointHighlight(GridCoords position, float opacity);
    void drawIntersections(std::span<const glm::vec3> intersections);
    /** @brief Submits generic world/screen text, reusable by later UI adapters. */
    void drawText(std::span<const TextRun> runs, TextSpace space);
    /** @brief Draws a filled rectangle in logical window pixels for basic UI. */
    void drawScreenRect(CanvasViewport bounds, glm::vec4 color);

    struct ScreenRect
    {
        CanvasViewport bounds;
        glm::vec4 color;
    };

    /** Batches chronogram segments and guide lines in one screen-space submission. */
    void drawScreenRects(std::span<const ScreenRect> rectangles);
    /** @brief Draws a shader body in logical window pixels, reusing the canvas component mesh. */
    void drawScreenComponent(const ComponentBodyInstance& body);
    /** @brief Clips screen drawing to logical window bounds; nullopt restores full-window drawing.
     */
    void setScreenClip(std::optional<CanvasViewport> bounds);

    const FontMetrics& fontMetrics() const { return m_text.metrics(); }

    void drawLabels(std::span<const ComponentRenderData> components);
    void drawDebugOverlay(const DebugMetrics& metrics, bool showMetrics = true);

    int getDrawCallCount() const { return m_drawCalls.total(); }

    const DrawCallCounts& getDrawCallCounts() const { return m_drawCalls; }

    /** Classifies screen submissions; canvas methods classify their own draws. */
    void setUiDrawCategory(DrawCategory category);
    void beginUiTiming(bool enabled);
    CpuTimings endUiTiming();

  private:
    Shader* acquireShader(const std::string& name);
    ShaderManager m_sm;
    std::unordered_set<std::string> m_missingShaderWarned;
    std::unique_ptr<Mesh> m_gateMesh, m_gridMesh, m_pointMesh, m_wireMesh, m_leadMesh, m_boundsMesh;
    // Reused per-pass staging; contents are rebuilt from current presentation values.
    std::vector<float> m_wireData, m_leadData, m_pinData, m_intersectionData;
    TextPainter m_text;
    CanvasCameraFrame m_currentCamera{};
    std::optional<PixelViewport> m_screenClip;
    bool setCanvasViewport();
    bool useCanvasShader(Shader& shader);
    void setScreenViewport();
    void recordDrawCalls(DrawCategory category, int count = 1);
    CpuTimings m_uiCpuTimes;
    bool m_timingUi = false;
    std::chrono::steady_clock::time_point m_uiTimeStart;
    DrawCallCounts m_drawCalls;
    DrawCategory m_uiDrawCategory = DrawCategory::StaticUI;
    bool m_countDrawCalls = true;
};
