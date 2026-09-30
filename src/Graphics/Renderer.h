#pragma once

#include "..\Logic\Circuit.h" // Provides EvalOrderResult
#include "..\Logic\Wire.h"
#include "..\Views\ComponentView.h"
#include "Mesh.h"
#include "ShaderManager.h"
#include "Text/FontRenderer.h"

#include <glm/glm.hpp>
#include <iomanip>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// A clean way to pass all frame-specific camera and window data
struct CameraState
{
    glm::vec2 panOffset;
    float zoom;
    float aspectRatio;
    int windowWidth;
    int windowHeight;
};

// Debugging metrics passed to HUD
struct DebugMetrics
{
    float fps;
    float frameTimeMs;
    int drawCalls;
    float lastPropagateMs;
    float timeSinceLastPropagateMs;
    size_t evalOrderCount;
    size_t totalComponents;
    EvalOrderResult evalResult;
    size_t netCount;
    size_t wireCount;
    size_t shortedNetCount;
    int hoveredCompId;
    int hoveredPinComponentId;
    int hoveredPinIdx;
    PinType hoveredPinType;
    WireId hoveredWireId;
    int selectedCompId;
    GridCoords cursorGrid;
};

class Renderer
{
  public:
    Renderer();
    ~Renderer();

    void init();

    /**
     * @brief Releases every GL resource the renderer owns (meshes and shaders).
     * Must run while the context is still current - i.e. before
     * glfwDestroyWindow() / glfwTerminate() - because GL objects must not
     * outlive their context. Safe to call more than once.
     */
    void shutdown();

    void beginFrame(const CameraState& camera);

    void drawGrid();

    // Data-driven: iterates whatever ComponentViews exist, grouping by shader
    // so same-shader components can still be instance-drawn together.
    void
    drawComponents(const std::unordered_map<int, std::unique_ptr<ComponentView>>& componentViews);

    void drawPins(
        const std::unordered_map<int, std::unique_ptr<ComponentView>>& componentViews,
        int hoveredCompId = -1,
        int hoveredPinIdx = -1,
        PinType hoveredPinType = PinType::INPUT
    );

    // Wires are keyed by their stable WireId; see Scene::getWires().
    void drawWires(const std::map<WireId, Wire>& wires, const Wire* activeWire = nullptr);

    void drawWireSegmentBoundingBox(
        const GridCoords& start, const GridCoords& end, float padding = 0.03f, float alpha = 1.0f
    );

    void drawComponentBoundingBox(
        const ComponentView& component, float padding = 0.3f, float alpha = 1.0f
    );

    void drawGridPointHighlight(GridCoords gridPos, float opacity);

    void drawIntersections(const std::vector<glm::vec3>& intersectionData);

    void drawText(
        const std::string& text, float worldX, float worldY, float size, const glm::vec4& color
    );

    void drawLabels(const std::unordered_map<int, std::unique_ptr<ComponentView>>& componentViews);

    void drawDebugOverlay(const DebugMetrics& metrics);

    int getDrawCallCount() const { return m_drawCallCount; }


  private:
    /**
     * @brief Looks a shader up by name, logging (at most once per name) the
     * ones that were never registered in init().
     * @param name Name the shader was registered under.
     * @return The shader, or nullptr when nothing was registered under that name.
     */
    Shader* acquireShader(const std::string& name);

    ShaderManager m_sm;

    // Names already reported by acquireShader(), so a component with a missing
    // shader produces one actionable error instead of one per frame.
    std::unordered_set<std::string> m_missingShaderWarned;

    std::unique_ptr<Mesh> m_gateMesh;
    std::unique_ptr<Mesh> m_gridMesh;
    std::unique_ptr<Mesh> m_pointMesh;
    std::unique_ptr<Mesh> m_wireMesh;
    std::unique_ptr<Mesh> m_boundsMesh;

    FontAtlas m_font;
    std::unique_ptr<Mesh> m_textMesh;

    CameraState m_currentCamera;

    int m_drawCallCount = 0;
};