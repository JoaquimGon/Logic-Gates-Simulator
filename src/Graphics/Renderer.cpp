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
    // Adjust these paths if your 'shaders' folder is relative to the executable differently now
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

    // 4. Wire Mesh (Dynamic batching, initialized empty)
    VertexLayout wireLayout;
    wireLayout.addAttribute(3); // Location 0: Position (X, Y, Z)
    wireLayout.addAttribute(4); // Location 1: Color (R, G, B, A)
    m_wireMesh = std::make_unique<Mesh>(std::vector<float>{}, std::vector<unsigned int>{}, wireLayout, GL_LINES);
}

void Renderer::beginFrame(const CameraState& camera)
{
    // Cache the camera so draw calls don't need it passed individually
    m_currentCamera = camera;

    // Clear the screen for the new frame
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

void Renderer::drawGates(const std::vector<GateView>& gatesViews)
{
    auto* shader = m_sm.get("ANDgate");
    shader->use();
    shader->setVec2("uPanOffset", m_currentCamera.panOffset.x, m_currentCamera.panOffset.y);
    shader->setFloat("uZoom", m_currentCamera.zoom);
    shader->setFloat("uAspectRatio", m_currentCamera.aspectRatio);
    shader->setVec2("uGateSize", 0.2f, 0.2f); // Uniform size for now

    std::vector<float> gatePositions;
    gatePositions.reserve(gatesViews.size() * 2); // Small optimization to prevent re-allocations

    for (const auto& gateView : gatesViews) {
        gatePositions.push_back(gateView.getPosition().x);
        gatePositions.push_back(gateView.getPosition().y);
    }

    if (!gatePositions.empty()) {
        m_gateMesh->setInstanceData(gatePositions, { 2 }, 1);
        m_gateMesh->drawInstanced(gatesViews.size());
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

    // 1. Batch all the permanent wires (reading by const reference, NO COPYING!)
    for (const auto& wire : wires) {
        std::vector<float> singleWireData = wire.getBatchedVertexData();
        allWiresData.insert(allWiresData.end(), singleWireData.begin(), singleWireData.end());
    }

    // 2. Batch the active wire if the user is currently drawing one
    if (activeWire != nullptr) {
        std::vector<float> singleWireData = activeWire->getBatchedVertexData();
        allWiresData.insert(allWiresData.end(), singleWireData.begin(), singleWireData.end());
    }

    if (!allWiresData.empty()) {
        m_wireMesh->updateData(allWiresData, 7);
        m_wireMesh->draw();
    }
}

void Renderer::drawPins(const std::vector<GateView>& gatesViews)
{
    auto* shader = m_sm.get("pin");
    shader->use();
    shader->setVec2("uPanOffset", m_currentCamera.panOffset.x, m_currentCamera.panOffset.y);
    shader->setFloat("uZoom", m_currentCamera.zoom);
    shader->setFloat("uAspectRatio", m_currentCamera.aspectRatio);
    shader->setFloat("uPointSize", 10.0f);

    std::vector<float> pinInstanceData;
    int totalPins = 0;

    for (const auto& gateView : gatesViews) {
        for (size_t gateId = 0; gateId < gatesViews.size(); ++gateId) {
            const auto& gateView = gatesViews[gateId];

            // Create a quick reusable lambda for drawing a pin
            auto processPin = [&](const PinUI& pin) {
                glm::vec2 pinWorldPos = gateView.getAbsolutePinWorldPos(pin);
                pinInstanceData.push_back(pinWorldPos.x);
                pinInstanceData.push_back(pinWorldPos.y);

                // Don't forget your Yellow hover highlight logic here if this is the Renderer!

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

            // Run the lambda on both arrays!
            for (const auto& pin : gateView.m_inputs)  processPin(pin);
            for (const auto& pin : gateView.m_outputs) processPin(pin);
        }
    }

    if (totalPins > 0) {
        m_pointMesh->setInstanceData(pinInstanceData, { 2, 4 }, 1);
        m_pointMesh->drawInstanced(totalPins);
    }
}