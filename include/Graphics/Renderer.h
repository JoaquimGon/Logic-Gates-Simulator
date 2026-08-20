#pragma once

#include <vector>
#include <memory>
#include <glm/glm.hpp>

#include "ShaderManager.h"
#include "Mesh.h"
#include "..\Engine\GateView.h"
#include "..\Engine\Wire.h"

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

    // Initializes OpenGL state, loads shaders, and builds the baseline meshes
    void init();

    // Prepares the renderer for a new frame (clears screen, caches camera)
    void beginFrame(const CameraState& camera);

    // Specific draw routines
    void drawGrid();
    void drawGates(const std::vector<GateView>& gatesViews);
    void drawWires(const std::vector<Wire>& wires, const Wire* activeWire = nullptr);
    void drawPins(const std::vector<GateView>& gatesViews);

private:
    ShaderManager m_sm;

    std::unique_ptr<Mesh> m_gateMesh;
    std::unique_ptr<Mesh> m_gridMesh;
    std::unique_ptr<Mesh> m_pointMesh;
    std::unique_ptr<Mesh> m_wireMesh;

    CameraState m_currentCamera;
};