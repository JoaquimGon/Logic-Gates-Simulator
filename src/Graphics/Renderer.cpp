#include "..\..\include\Graphics\Renderer.h"
#include <glad/glad.h>

Renderer::Renderer()
{
}

Renderer::~Renderer()
{
}

void Renderer::init()
{
    // ==========================================
    // OpenGL State Configuration
    // ==========================================
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glClearColor(0.05f, 0.05f, 0.05f, 1.0f);
    glEnable(GL_PROGRAM_POINT_SIZE);

    // ==========================================
    // Shaders
    // ==========================================
    m_sm.load("ANDgate", "shaders/andGate.vert", "shaders/andGate.frag");
    m_sm.load("grid", "shaders/vec4Shader.vert", "shaders/grid.frag");
    m_sm.load("pin", "shaders/pins/pins.vert", "shaders/pins/pins.frag");
    m_sm.load("wire", "shaders/wires/wires.vert", "shaders/wires/wires.frag");

    // ==========================================
    // Meshes
    // ==========================================
    // 1. Gate Mesh
    std::vector<float> quadVertices = {
        -0.5f,  0.5f, 0.0,
        -0.5f, -0.5f, 0.0,
         0.5f, -0.5f, 0.0,
         0.5f,  0.5f, 0.0
    };
    std::vector<unsigned int> quadIndices = { 0, 1, 2, 2, 3, 0 };
    VertexLayout gateLayout;
    gateLayout.addAttribute(3);
    m_gateMesh = std::make_unique<Mesh>(quadVertices, quadIndices, gateLayout, GL_TRIANGLES);

    // 2. Grid Mesh
    std::vector<float> fullScreenVertices = {
        -1.0f,  1.0f, -1.0f, -1.0f, -1.0f, -1.0f,
         1.0f, -1.0f, -1.0f,  1.0f,  1.0f, -1.0f
    };
    std::vector<unsigned int> fullScreenIndeces = { 0, 1, 2, 2, 3, 0 };
    VertexLayout gridLayout;
    gridLayout.addAttribute(3);
    m_gridMesh = std::make_unique<Mesh>(fullScreenVertices, fullScreenIndeces, gridLayout, GL_TRIANGLES);

    // 3. Pin Mesh
    std::vector<float> pointVertices = { 0.0f, 0.0f, 0.0f };
    VertexLayout pointLayout;
    pointLayout.addAttribute(3);
    m_pointMesh = std::make_unique<Mesh>(pointVertices, std::vector<unsigned int>{}, pointLayout, GL_POINTS);

    // 4. Wire Mesh 
    VertexLayout wireLayout;
    wireLayout.addAttribute(3); // Location 0: Position (X, Y, Z)
    wireLayout.addAttribute(4); // Location 1: Color (R, G, B, A)
    m_wireMesh = std::make_unique<Mesh>(std::vector<float>{}, std::vector<unsigned int>{}, wireLayout, GL_LINES);

    // ==========================================
    // NEW: 5. Bounding Box Mesh
    // ==========================================
    // We use GL_LINES so we can draw crisp edges, reusing the exact same layout as wires!
    VertexLayout boundsLayout;
    boundsLayout.addAttribute(3); // Position
    boundsLayout.addAttribute(4); // Color
    m_boundsMesh = std::make_unique<Mesh>(std::vector<float>{}, std::vector<unsigned int>{}, boundsLayout, GL_LINES);
}

void Renderer::beginFrame(const CameraState& camera)
{
    m_currentCamera = camera;
    glClear(GL_COLOR_BUFFER_BIT);
}

void Renderer::drawGrid()
{
    auto* shader = m_sm.get("grid");
    shader->use();
    shader->setVec2("uPanOffset", m_currentCamera.panOffset.x, m_currentCamera.panOffset.y);
    shader->setFloat("uZoom", m_currentCamera.zoom);
    shader->setFloat("uGridSpacing", 0.05f);
    shader->setVec2("uResolution", static_cast<float>(m_currentCamera.windowWidth), static_cast<float>(m_currentCamera.windowHeight));

    m_gridMesh->draw();
}

void Renderer::drawGates(const std::unordered_map<int, GateView>& gateViews)
{
    auto* shader = m_sm.get("ANDgate");
    shader->use();
    shader->setVec2("uPanOffset", m_currentCamera.panOffset.x, m_currentCamera.panOffset.y);
    shader->setFloat("uZoom", m_currentCamera.zoom);
    shader->setFloat("uAspectRatio", m_currentCamera.aspectRatio);
    shader->setVec2("uGateSize", 0.2f, 0.2f);

    std::vector<float> gatePositions;
    gatePositions.reserve(gateViews.size() * 2);

    for (const auto& [id, gateView] : gateViews) {
        gatePositions.push_back(gateView.getPosition().x);
        gatePositions.push_back(gateView.getPosition().y);
    }

    if (!gatePositions.empty()) {
        m_gateMesh->setInstanceData(gatePositions, { 2 }, 1);
        m_gateMesh->drawInstanced(static_cast<unsigned int>(gateViews.size()));
    }
}

void Renderer::drawWires(const std::vector<Wire>& wires, const Wire* activeWire)
{
    auto* shader = m_sm.get("wire");
    shader->use();
    shader->setVec2("uPanOffset", m_currentCamera.panOffset.x, m_currentCamera.panOffset.y);
    shader->setFloat("uZoom", m_currentCamera.zoom);
    shader->setFloat("uAspectRatio", m_currentCamera.aspectRatio);

    std::vector<float> allWiresData;

    for (const auto& wire : wires) {
        std::vector<float> singleWireData = wire.getBatchedVertexData();
        allWiresData.insert(allWiresData.end(), singleWireData.begin(), singleWireData.end());
    }

    if (activeWire != nullptr) {
        std::vector<float> singleWireData = activeWire->getBatchedVertexData();
        allWiresData.insert(allWiresData.end(), singleWireData.begin(), singleWireData.end());
    }

    if (!allWiresData.empty()) {
        m_wireMesh->updateData(allWiresData, 7);
        m_wireMesh->draw();
    }
}

void Renderer::drawPins(const std::unordered_map<int, GateView>& gateViews)
{
    auto* shader = m_sm.get("pin");
    shader->use();
    shader->setVec2("uPanOffset", m_currentCamera.panOffset.x, m_currentCamera.panOffset.y);
    shader->setFloat("uZoom", m_currentCamera.zoom);
    shader->setFloat("uAspectRatio", m_currentCamera.aspectRatio);
    shader->setFloat("uPointSize", 10.0f);

    std::vector<float> pinInstanceData;
    int totalPins = 0;



    // FIX: Removed the redundant double-loop here!
    for (size_t gateId = 0; gateId < gateViews.size(); ++gateId) {
        const auto& gateView = gateViews.at(gateId);

        auto processPin = [&](const PinUI& pin) {
            glm::vec2 pinWorldPos = gateView.getAbsolutePinWorldPos(pin);
            pinInstanceData.push_back(pinWorldPos.x);
            pinInstanceData.push_back(pinWorldPos.y);

            if (pin.state == PinState::DISCONNECTED) {
                pinInstanceData.insert(pinInstanceData.end(), { 0.0f, 0.0f, 1.0f, 1.0f });
            }
            else if (pin.state == PinState::ON) {
                pinInstanceData.insert(pinInstanceData.end(), { 0.0f, 1.0f, 0.0f, 1.0f });
            }
            else {
                pinInstanceData.insert(pinInstanceData.end(), { 1.0f, 0.0f, 0.0f, 1.0f });
            }
            totalPins++;
            };

        for (const auto& pin : gateView.m_inputs)  processPin(pin);
        for (const auto& pin : gateView.m_outputs) processPin(pin);
    }

    if (totalPins > 0) {
        m_pointMesh->setInstanceData(pinInstanceData, { 2, 4 }, 1);
        m_pointMesh->drawInstanced(totalPins);
    }
}

void Renderer::drawWireBoundingBox(const Wire& wire, float padding)
{
    if (wire.getPath().empty()) return;

    auto* shader = m_sm.get("wire");
    shader->use();
    shader->setVec2("uPanOffset", m_currentCamera.panOffset.x, m_currentCamera.panOffset.y);
    shader->setFloat("uZoom", m_currentCamera.zoom);
    shader->setFloat("uAspectRatio", m_currentCamera.aspectRatio);

    float minX = 999999.0f, minY = 999999.0f;
    float maxX = -999999.0f, maxY = -999999.0f;

    // Find the furthest edges of the wire
    for (const auto& gridPos : wire.getPath()) {
        glm::vec2 worldPos = GridSystem::gridToWorld(gridPos);
        if (worldPos.x < minX) minX = worldPos.x;
        if (worldPos.x > maxX) maxX = worldPos.x;
        if (worldPos.y < minY) minY = worldPos.y;
        if (worldPos.y > maxY) maxY = worldPos.y;
    }

    // Apply the visual padding
    minX -= padding;
    maxX += padding;
    minY -= padding;
    maxY += padding;

    // Convert (255, 159, 28) to Normalized RGB
    float r = 255.0f / 255.0f;
    float g = 159.0f / 255.0f;
    float b = 28.0f / 255.0f;
    float a = 1.0f;

    std::vector<float> boxData = {
        minX, maxY, 0.0f, r, g, b, a,
        maxX, maxY, 0.0f, r, g, b, a,

        maxX, maxY, 0.0f, r, g, b, a,
        maxX, minY, 0.0f, r, g, b, a,

        maxX, minY, 0.0f, r, g, b, a,
        minX, minY, 0.0f, r, g, b, a,

        minX, minY, 0.0f, r, g, b, a,
        minX, maxY, 0.0f, r, g, b, a
    };

    m_boundsMesh->updateData(boxData, 7);
    m_boundsMesh->draw();
}

void Renderer::drawGateBoundingBox(const GateView& gate, float padding)
{
    // We reuse the wire shader since a bounding box is just 4 colored lines
    auto* shader = m_sm.get("wire");
    shader->use();
    shader->setVec2("uPanOffset", m_currentCamera.panOffset.x, m_currentCamera.panOffset.y);
    shader->setFloat("uZoom", m_currentCamera.zoom);
    shader->setFloat("uAspectRatio", m_currentCamera.aspectRatio);

    glm::vec2 pos = gate.getPosition();
    glm::vec2 size = gate.getSize();

    // Scale outward from the center to get our padded corners
    float halfW = (size.x * 0.5f) + padding;
    float halfH = (size.y * 0.5f) + padding;

    glm::vec2 topLeft(-halfW + pos.x, halfH + pos.y);
    glm::vec2 topRight(halfW + pos.x, halfH + pos.y);
    glm::vec2 bottomLeft(-halfW + pos.x, -halfH + pos.y);
    glm::vec2 bottomRight(halfW + pos.x, -halfH + pos.y);

    // Convert requested (255, 159, 28) to Normalized RGB
    float r = 255.0f / 255.0f;
    float g = 159.0f / 255.0f;
    float b = 28.0f / 255.0f;
    float a = 1.0f;

    // GL_LINES requires endpoints for each individual segment: (A->B), (B->C), (C->D), (D->A)
    std::vector<float> boxData = {
        // Top Line
        topLeft.x, topLeft.y, 0.0f, r, g, b, a,
        topRight.x, topRight.y, 0.0f, r, g, b, a,

        // Right Line
        topRight.x, topRight.y, 0.0f, r, g, b, a,
        bottomRight.x, bottomRight.y, 0.0f, r, g, b, a,

        // Bottom Line
        bottomRight.x, bottomRight.y, 0.0f, r, g, b, a,
        bottomLeft.x, bottomLeft.y, 0.0f, r, g, b, a,

        // Left Line
        bottomLeft.x, bottomLeft.y, 0.0f, r, g, b, a,
        topLeft.x, topLeft.y, 0.0f, r, g, b, a
    };

    m_boundsMesh->updateData(boxData, 7); // 3 Position + 4 Color = 7 floats per vertex
    m_boundsMesh->draw();
}

void Renderer::drawWireSegmentBoundingBox(const GridCoords& start, const GridCoords& end, float padding)
{
    auto* shader = m_sm.get("wire");
    shader->use();
    shader->setVec2("uPanOffset", m_currentCamera.panOffset.x, m_currentCamera.panOffset.y);
    shader->setFloat("uZoom", m_currentCamera.zoom);
    shader->setFloat("uAspectRatio", m_currentCamera.aspectRatio);

    glm::vec2 p1 = GridSystem::gridToWorld(start);
    glm::vec2 p2 = GridSystem::gridToWorld(end);

    float minX = std::min(p1.x, p2.x) - padding;
    float maxX = std::max(p1.x, p2.x) + padding;
    float minY = std::min(p1.y, p2.y) - padding;
    float maxY = std::max(p1.y, p2.y) + padding;

    float r = 255.0f / 255.0f;
    float g = 159.0f / 255.0f;
    float b = 28.0f / 255.0f;
    float a = 1.0f;

    std::vector<float> boxData = {
        minX, maxY, 0.0f, r, g, b, a,
        maxX, maxY, 0.0f, r, g, b, a,

        maxX, maxY, 0.0f, r, g, b, a,
        maxX, minY, 0.0f, r, g, b, a,

        maxX, minY, 0.0f, r, g, b, a,
        minX, minY, 0.0f, r, g, b, a,

        minX, minY, 0.0f, r, g, b, a,
        minX, maxY, 0.0f, r, g, b, a
    };

    m_boundsMesh->updateData(boxData, 7);
    m_boundsMesh->draw();
}