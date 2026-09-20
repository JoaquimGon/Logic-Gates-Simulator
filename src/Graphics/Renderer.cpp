#include "Renderer.h"
#include <glad/glad.h>
#include <iostream>

Renderer::Renderer()
{
}

Renderer::~Renderer()
{
    // Safety net only: the engine calls shutdown() while the context is still
    // alive, since deleting GL objects after glfwTerminate() is invalid.
    shutdown();
}

void Renderer::shutdown()
{
    // reset() destroys each Mesh, whose destructor releases its GL buffers.
    m_gateMesh.reset();
    m_gridMesh.reset();
    m_pointMesh.reset();
    m_wireMesh.reset();
    m_boundsMesh.reset();

    // Runs every Shader destructor, i.e. glDeleteProgram(), before the context dies.
    m_sm.release();
}

Shader* Renderer::acquireShader(const std::string& name)
{
    if (Shader* shader = m_sm.get(name))
        return shader;

    // A miss here is a programming error: some component was registered with a
    // shader name that init() never loaded, so nothing would ever be drawn for
    // it. Report it once per name instead of failing silently every frame.
    if (m_missingShaderWarned.insert(name).second)
    {
        std::cerr << "[Renderer] No shader registered under the name \"" << name
            << "\" - that component will not be drawn. Register it in Renderer::init().\n";
    }
    return nullptr;
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
    // 
    // .load will attempt to use the shaders in the build folder, which is corrected by
    // cmake rebuilding them in said folder by copying the "assets/shaders" folder
    // Always start path with the "shaders" folder cmake will do the rest

    // Gates
    m_sm.load("ANDgate", "shaders/gates/gate.vert", "shaders/gates/andGate.frag");
    m_sm.load("ORgate", "shaders/gates/gate.vert", "shaders/gates/orGate.frag");
    m_sm.load("XORgate", "shaders/gates/gate.vert", "shaders/gates/xorGate.frag");
    m_sm.load("NOTgate", "shaders/gates/gate.vert", "shaders/gates/notGate.frag");
    // Manual input switch (InputPinView). Shares the gate vertex shader so the
    // SDF body receives localPos in the same -0.5..0.5 space as the gates.
    m_sm.load("inputPin", "shaders/gates/gate.vert", "shaders/gates/inputPin.frag");

    // Grid
    m_sm.load("grid", "shaders/vec3Shader.vert", "shaders/grid.frag");

    // Pins
    m_sm.load("pin", "shaders/pins/pins.vert", "shaders/pins/pins.frag");

    // Wires
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
    auto* shader = acquireShader("grid");
    if (!shader) return;
    shader->use();
    shader->setVec2("uPanOffset", m_currentCamera.panOffset.x, m_currentCamera.panOffset.y);
    shader->setFloat("uZoom", m_currentCamera.zoom);
    shader->setFloat("uGridSpacing", 0.05f);
    shader->setVec2("uResolution", static_cast<float>(m_currentCamera.windowWidth), static_cast<float>(m_currentCamera.windowHeight));

    m_gridMesh->draw();
}

void Renderer::drawWires(const std::map<WireId, Wire>& wires, const Wire* activeWire)
{
    auto* shader = acquireShader("wire");
    if (!shader) return;
    shader->use();
    shader->setVec2("uPanOffset", m_currentCamera.panOffset.x, m_currentCamera.panOffset.y);
    shader->setFloat("uZoom", m_currentCamera.zoom);
    shader->setFloat("uAspectRatio", m_currentCamera.aspectRatio);

    std::vector<float> allWiresData;

    for (const auto& [id, wire] : wires) {
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

void Renderer::drawWireBoundingBox(const Wire& wire, float padding, float alpha)
{
    if (wire.getPath().empty()) return;

    auto* shader = acquireShader("wire");
    if (!shader) return;
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
    float r = 255.0f / 255.0f, g = 159.0f / 255.0f, b = 28.0f / 255.0f, a = alpha;

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

void Renderer::drawWireSegmentBoundingBox(const GridCoords& start, const GridCoords& end, float padding, float alpha)
{
    auto* shader = acquireShader("wire");
    if (!shader) return;
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
    float a = alpha;

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

void Renderer::drawComponents(const std::unordered_map<int, std::unique_ptr<ComponentView>>& componentViews)
{
    // Group by shader so components sharing a shader still get instanced together.
    std::unordered_map<std::string, std::vector<glm::vec2>> positionsByShader;
    std::unordered_map<std::string, glm::vec2> sizeByShader;

    for (const auto& [id, view] : componentViews) {
        positionsByShader[view->getShaderName()].push_back(view->getPosition());
        sizeByShader[view->getShaderName()] = view->getSize();
    }

    for (auto& [shaderName, positions] : positionsByShader) {
        auto* shader = acquireShader(shaderName);
        if (!shader) continue; // acquireShader already logged the missing name

        shader->use();
        shader->setVec2("uPanOffset", m_currentCamera.panOffset.x, m_currentCamera.panOffset.y);
        shader->setFloat("uZoom", m_currentCamera.zoom);
        shader->setFloat("uAspectRatio", m_currentCamera.aspectRatio);
        shader->setVec2("uGateSize", sizeByShader[shaderName].x, sizeByShader[shaderName].y);

        std::vector<float> flatPositions;
        flatPositions.reserve(positions.size() * 2);
        for (const auto& p : positions) {
            flatPositions.push_back(p.x);
            flatPositions.push_back(p.y);
        }

        m_gateMesh->setInstanceData(flatPositions, { 2 }, 1);
        m_gateMesh->drawInstanced(static_cast<unsigned int>(positions.size()));
    }
}

void Renderer::drawPins(const std::unordered_map<int, std::unique_ptr<ComponentView>>& componentViews, int hoveredCompId, int hoveredPinIdx, PinType hoveredPinType)
{
    auto* shader = acquireShader("pin");
    if (!shader) return;
    shader->use();
    shader->setVec2("uPanOffset", m_currentCamera.panOffset.x, m_currentCamera.panOffset.y);
    shader->setFloat("uZoom", m_currentCamera.zoom);
    shader->setFloat("uAspectRatio", m_currentCamera.aspectRatio);
    shader->setFloat("uPointSize", 10.0f);

    std::vector<float> pinInstanceData;
    int totalPins = 0;

    for (const auto& [id, view] : componentViews) {
        auto processPin = [&](const PinUI& pin) {
            glm::vec2 pinWorldPos = view->getAbsolutePinWorldPos(pin);
            pinInstanceData.push_back(pinWorldPos.x);
            pinInstanceData.push_back(pinWorldPos.y);

            if (id == hoveredCompId && pin.pin_index == hoveredPinIdx && pin.type == hoveredPinType) {
                float r = 255.0f / 255.0f, g = 159.0f / 255.0f, b = 28.0f / 255.0f, a = 1;
                pinInstanceData.insert(pinInstanceData.end(), { r, g, b, a }); // Same colour as the bounding box
            }
            else {
                if (pin.state == PinState::DISCONNECTED)  pinInstanceData.insert(pinInstanceData.end(), { 0.0f, 0.0f, 1.0f, 1.0f });
                else if (pin.state == PinState::ON)        pinInstanceData.insert(pinInstanceData.end(), { 0.0f, 1.0f, 0.0f, 1.0f });
                else                                        pinInstanceData.insert(pinInstanceData.end(), { 1.0f, 0.0f, 0.0f, 1.0f });
            }
            totalPins++;
            };

        for (const auto& pin : view->getInputPins())  processPin(pin);
        for (const auto& pin : view->getOutputPins()) processPin(pin);
    }

    if (totalPins > 0) {
        m_pointMesh->setInstanceData(pinInstanceData, { 2, 4 }, 1);
        m_pointMesh->drawInstanced(totalPins);
    }
}

void Renderer::drawComponentBoundingBox(const ComponentView& component, float padding, float alpha)
{
    auto* shader = acquireShader("wire"); // reused: a bounding box is just 4 colored lines
    if (!shader) return;
    shader->use();
    shader->setVec2("uPanOffset", m_currentCamera.panOffset.x, m_currentCamera.panOffset.y);
    shader->setFloat("uZoom", m_currentCamera.zoom);
    shader->setFloat("uAspectRatio", m_currentCamera.aspectRatio);

    glm::vec2 pos = component.getPosition();
    glm::vec2 size = component.getSize();

    float halfW = (size.x * 0.5f) + padding;
    float halfH = (size.y * 0.5f) + padding;

    glm::vec2 topLeft(-halfW + pos.x, halfH + pos.y);
    glm::vec2 topRight(halfW + pos.x, halfH + pos.y);
    glm::vec2 bottomLeft(-halfW + pos.x, -halfH + pos.y);
    glm::vec2 bottomRight(halfW + pos.x, -halfH + pos.y);

    float r = 255.0f / 255.0f, g = 159.0f / 255.0f, b = 28.0f / 255.0f, a = alpha;

    std::vector<float> boxData = {
        topLeft.x, topLeft.y, 0.0f, r, g, b, a,
        topRight.x, topRight.y, 0.0f, r, g, b, a,

        topRight.x, topRight.y, 0.0f, r, g, b, a,
        bottomRight.x, bottomRight.y, 0.0f, r, g, b, a,

        bottomRight.x, bottomRight.y, 0.0f, r, g, b, a,
        bottomLeft.x, bottomLeft.y, 0.0f, r, g, b, a,

        bottomLeft.x, bottomLeft.y, 0.0f, r, g, b, a,
        topLeft.x, topLeft.y, 0.0f, r, g, b, a
    };

    m_boundsMesh->updateData(boxData, 7);
    m_boundsMesh->draw();
}

void Renderer::drawGridPointHighlight(GridCoords gridPos, float opacity)
{
    auto* shader = acquireShader("pin");
    if (!shader) return;
    shader->use();
    shader->setVec2("uPanOffset", m_currentCamera.panOffset.x, m_currentCamera.panOffset.y);
    shader->setFloat("uZoom", m_currentCamera.zoom);
    shader->setFloat("uAspectRatio", m_currentCamera.aspectRatio);
    shader->setFloat("uPointSize", 8.0f); // Slightly smaller than standard pins (10.0f)

    glm::vec2 worldPos = GridSystem::gridToWorld(gridPos);

    // Orange color matching your bounding boxes
    float r = 255.0f / 255.0f;
    float g = 159.0f / 255.0f;
    float b = 28.0f / 255.0f;

    // Send the single point to the instanced mesh
    std::vector<float> data = { worldPos.x, worldPos.y, r, g, b, opacity };
    m_pointMesh->setInstanceData(data, { 2, 4 }, 1);
    m_pointMesh->drawInstanced(1);
}


void Renderer::drawIntersections(const std::vector<glm::vec3>& intersectionData)
{
    if (intersectionData.empty()) return;

    auto* shader = acquireShader("pin");
    if (!shader) return;
    shader->use();
    shader->setVec2("uPanOffset", m_currentCamera.panOffset.x, m_currentCamera.panOffset.y);
    shader->setFloat("uZoom", m_currentCamera.zoom);
    shader->setFloat("uAspectRatio", m_currentCamera.aspectRatio);
    shader->setFloat("uPointSize", 9.0f); // 0.9 ratio relative to standard 10.0f pins

    std::vector<float> instancedData;
    instancedData.reserve(intersectionData.size() * 6); // 2 pos + 4 color

    for (const auto& data : intersectionData) {
        glm::vec2 worldPos = GridSystem::gridToWorld({ static_cast<int>(data.x), static_cast<int>(data.y) });
        instancedData.push_back(worldPos.x);
        instancedData.push_back(worldPos.y);

        // Map the state back to colors (0=DISCONNECTED(Blue), 1=ON(Green), 2=OFF(Red))
        if (data.z == 0.0f)      instancedData.insert(instancedData.end(), { 0.0f, 0.0f, 1.0f, 1.0f });
        else if (data.z == 1.0f) instancedData.insert(instancedData.end(), { 0.0f, 1.0f, 0.0f, 1.0f });
        else                     instancedData.insert(instancedData.end(), { 1.0f, 0.0f, 0.0f, 1.0f });
    }

    m_pointMesh->setInstanceData(instancedData, { 2, 4 }, 1);
    m_pointMesh->drawInstanced(static_cast<int>(intersectionData.size()));
}