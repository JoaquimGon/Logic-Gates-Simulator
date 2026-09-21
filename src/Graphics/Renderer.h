#pragma once
#include <vector>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <map>
#include <glm/glm.hpp>

#include "ShaderManager.h"
#include "Mesh.h"
#include "..\Views\ComponentView.h"
#include "..\Logic\Wire.h"


// A clean way to pass all frame-specific camera and window data
struct CameraState {
    glm::vec2 panOffset;
    float zoom;
    float aspectRatio;
    int windowWidth;
    int windowHeight;
};

class Renderer {
public:
    Renderer();
    ~Renderer();

    void init();

    /**
    * @brief Releases every GL resource the renderer owns (meshes and shaders).
    * Must run while the context is still current - i.e. before glfwDestroyWindow()
    * / glfwTerminate() - because GL objects must not outlive their context. Safe to
    * call more than once.
    */
    void shutdown();

    void beginFrame(const CameraState& camera);

    void drawGrid();

    // Data-driven: iterates whatever ComponentViews exist, grouping by shader
    // so same-shader components can still be instance-drawn together.
    void drawComponents(const std::unordered_map<int, std::unique_ptr<ComponentView>>& componentViews);
    void drawPins(const std::unordered_map<int, std::unique_ptr<ComponentView>>& componentViews, int hoveredCompId = -1, int hoveredPinIdx = -1, PinType hoveredPinType = PinType::INPUT);

    // Wires are keyed by their stable WireId; see Scene::getWires().
    void drawWires(const std::map<WireId, Wire>& wires, const Wire* activeWire = nullptr);

    // Change these three lines in Renderer.h:
    void drawWireSegmentBoundingBox(const GridCoords& start, const GridCoords& end, float padding = 0.03f, float alpha = 1.0f);
    void drawComponentBoundingBox(const ComponentView& component, float padding = 0.3f, float alpha = 1.0f);
    void drawGridPointHighlight(GridCoords gridPos, float opacity);
    void drawIntersections(const std::vector<glm::vec3>& intersectionData);
private:
    /**
    * @brief Looks a shader up by name, logging (at most once per name) the ones
    * that were never registered in init().
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

    CameraState m_currentCamera;
};