#pragma once
#include <vector>
#include <memory>
#include <string>
#include <unordered_map>
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
    void beginFrame(const CameraState& camera);

    void drawGrid();

    // Data-driven: iterates whatever ComponentViews exist, grouping by shader
    // so same-shader components can still be instance-drawn together.
    void drawComponents(const std::unordered_map<int, std::unique_ptr<ComponentView>>& componentViews);
    void drawPins(const std::unordered_map<int, std::unique_ptr<ComponentView>>& componentViews, int hoveredCompId = -1, int hoveredPinIdx = -1, PinType hoveredPinType = PinType::INPUT);

    void drawWires(const std::vector<Wire>& wires, const Wire* activeWire = nullptr);

    // Change these three lines in Renderer.h:
    void drawWireBoundingBox(const Wire& wire, float padding = 0.03f, float alpha = 1.0f);
    void drawWireSegmentBoundingBox(const GridCoords& start, const GridCoords& end, float padding = 0.03f, float alpha = 1.0f);
    void drawComponentBoundingBox(const ComponentView& component, float padding = 0.3f, float alpha = 1.0f);
    void drawGridPointHighlight(GridCoords gridPos, float opacity);
private:
    ShaderManager m_sm;

    std::unique_ptr<Mesh> m_gateMesh;
    std::unique_ptr<Mesh> m_gridMesh;
    std::unique_ptr<Mesh> m_pointMesh;
    std::unique_ptr<Mesh> m_wireMesh;
    std::unique_ptr<Mesh> m_boundsMesh;

    CameraState m_currentCamera;
};