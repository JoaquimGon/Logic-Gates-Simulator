#include "Renderer.h"
#include "..\Views\LatchView.h"
#include "..\Logic\Circuit.h"

#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>


Renderer::Renderer() {}

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
    m_font.destroy();
    m_textMesh.reset();

    // Runs every Shader destructor, i.e. glDeleteProgram(), before the context
    // dies.
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
                  << "\" - that component will not be drawn. Register it in "
                     "Renderer::init().\n";
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
    // Gates
    m_sm.load(
        "ANDgate", "shaders/components/gates/gate.vert", "shaders/components/gates/andGate.frag"
    );
    m_sm.load(
        "NANDgate", "shaders/components/gates/gate.vert", "shaders/components/gates/nandGate.frag"
    );
    m_sm.load(
        "ORgate", "shaders/components/gates/gate.vert", "shaders/components/gates/orGate.frag"
    );
    m_sm.load(
        "NORgate", "shaders/components/gates/gate.vert", "shaders/components/gates/norGate.frag"
    );
    m_sm.load(
        "XORgate", "shaders/components/gates/gate.vert", "shaders/components/gates/xorGate.frag"
    );
    m_sm.load(
        "NXORgate", "shaders/components/gates/gate.vert", "shaders/components/gates/nxorGate.frag"
    );
    m_sm.load(
        "NOTgate", "shaders/components/gates/gate.vert", "shaders/components/gates/notGate.frag"
    );

    // Latch & Clock & InputPin
    m_sm.load(
        "latch", "shaders/components/gates/gate.vert", "shaders/components/latches/latch.frag"
    );
    m_sm.load("clock", "shaders/components/gates/gate.vert", "shaders/components/clock.frag");
    m_sm.load("inputPin", "shaders/components/gates/gate.vert", "shaders/components/inputPin.frag");

    // Pins
    m_sm.load("pin", "shaders/components/pins.vert", "shaders/components/pins.frag");

    // Text
    m_sm.load("text", "shaders/text/text.vert", "shaders/text/text.frag");

    // Grid & Wires
    m_sm.load("grid", "shaders/vec3Shader.vert", "shaders/grid.frag");
    m_sm.load("wire", "shaders/wires/wires.vert", "shaders/wires/wires.frag");

    // ==========================================
    // Meshes
    // ==========================================
    // 1. Gate Mesh
    std::vector<float> quadVertices = {
        -0.5f, 0.5f, 0.0, -0.5f, -0.5f, 0.0, 0.5f, -0.5f, 0.0, 0.5f, 0.5f, 0.0
    };
    std::vector<unsigned int> quadIndices = {0, 1, 2, 2, 3, 0};
    VertexLayout gateLayout;
    gateLayout.addAttribute(3);
    m_gateMesh = std::make_unique<Mesh>(quadVertices, quadIndices, gateLayout, GL_TRIANGLES);

    // 2. Grid Mesh
    std::vector<float> fullScreenVertices = {
        -1.0f, 1.0f, -1.0f, -1.0f, -1.0f, -1.0f, 1.0f, -1.0f, -1.0f, 1.0f, 1.0f, -1.0f
    };
    std::vector<unsigned int> fullScreenIndeces = {0, 1, 2, 2, 3, 0};
    VertexLayout gridLayout;
    gridLayout.addAttribute(3);
    m_gridMesh =
        std::make_unique<Mesh>(fullScreenVertices, fullScreenIndeces, gridLayout, GL_TRIANGLES);

    // 3. Pin Mesh
    std::vector<float> pointVertices = {0.0f, 0.0f, 0.0f};
    VertexLayout pointLayout;
    pointLayout.addAttribute(3);
    m_pointMesh =
        std::make_unique<Mesh>(pointVertices, std::vector<unsigned int>{}, pointLayout, GL_POINTS);

    // 4. Wire Mesh
    VertexLayout wireLayout;
    wireLayout.addAttribute(3); // Location 0: Position (X, Y, Z)
    wireLayout.addAttribute(4); // Location 1: Color (R, G, B, A)
    m_wireMesh = std::make_unique<Mesh>(
        std::vector<float>{}, std::vector<unsigned int>{}, wireLayout, GL_TRIANGLES
    );

    // ==========================================
    // Bounding Box Mesh
    // ==========================================
    VertexLayout boundsLayout;
    boundsLayout.addAttribute(3); // Position
    boundsLayout.addAttribute(4); // Color
    m_boundsMesh = std::make_unique<Mesh>(
        std::vector<float>{}, std::vector<unsigned int>{}, boundsLayout, GL_LINES
    );

    // ==========================================
    // Text
    // ==========================================
    // Initialize Font Atlas (Uses Windows Consolas as default, or any TTF in assets)
    std::string fontPath = "C:/Windows/Fonts/consola.ttf";
    if (!m_font.init(fontPath, 48.0f))
    {
        // Fallback to Arial if Consolas isn't present
        m_font.init("C:/Windows/Fonts/arial.ttf", 48.0f);
    }

    // Text Mesh Dynamic Layout: Pos(2) + UV(2) + Color(4) = 8 floats
    VertexLayout textLayout;
    textLayout.addAttribute(2); // aPos
    textLayout.addAttribute(2); // aTexCoord
    textLayout.addAttribute(4); // aColor
    m_textMesh = std::make_unique<Mesh>(
        std::vector<float>{}, std::vector<unsigned int>{}, textLayout, GL_TRIANGLES
    );

}


void Renderer::beginFrame(const CameraState& camera)
{
    m_sm.checkHotReload();
    m_currentCamera = camera;
    m_drawCallCount = 0; // Reset at the beginning of each frame
    glClear(GL_COLOR_BUFFER_BIT);
}


void Renderer::drawGrid()
{
    auto* shader = acquireShader("grid");
    if (!shader)
        return;
    shader->use();
    shader->setVec2("uPanOffset", m_currentCamera.panOffset.x, m_currentCamera.panOffset.y);
    shader->setFloat("uZoom", m_currentCamera.zoom);
    shader->setFloat("uGridSpacing", 0.05f);
    shader->setVec2(
        "uResolution",
        static_cast<float>(m_currentCamera.windowWidth),
        static_cast<float>(m_currentCamera.windowHeight)
    );

    m_gridMesh->draw();
    m_drawCallCount++;
}


void Renderer::drawWires(const std::map<WireId, Wire>& wires, const Wire* activeWire)
{
    auto* shader = acquireShader("wire");
    if (!shader)
        return;
    shader->use();
    shader->setVec2("uPanOffset", m_currentCamera.panOffset.x, m_currentCamera.panOffset.y);
    shader->setFloat("uZoom", m_currentCamera.zoom);
    shader->setFloat("uAspectRatio", m_currentCamera.aspectRatio);

    std::vector<float> allWiresData;

    for (const auto& [id, wire] : wires)
    {
        std::vector<float> singleWireData = wire.getBatchedVertexData();
        allWiresData.insert(allWiresData.end(), singleWireData.begin(), singleWireData.end());
    }

    if (activeWire != nullptr)
    {
        std::vector<float> singleWireData = activeWire->getBatchedVertexData();
        allWiresData.insert(allWiresData.end(), singleWireData.begin(), singleWireData.end());
    }

    if (!allWiresData.empty())
    {
        m_wireMesh->updateData(allWiresData, 7);
        m_wireMesh->draw();
        m_drawCallCount++;
    }
}


void Renderer::drawWireSegmentBoundingBox(
    const GridCoords& start, const GridCoords& end, float padding, float alpha
)
{
    auto* shader = acquireShader("wire");
    if (!shader)
        return;
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

    std::vector<float> boxData = {minX, maxY, 0.0f, r, g, b, a, maxX, maxY, 0.0f, r, g, b, a,

                                  maxX, maxY, 0.0f, r, g, b, a, maxX, minY, 0.0f, r, g, b, a,

                                  maxX, minY, 0.0f, r, g, b, a, minX, minY, 0.0f, r, g, b, a,

                                  minX, minY, 0.0f, r, g, b, a, minX, maxY, 0.0f, r, g, b, a};

    m_boundsMesh->updateData(boxData, 7);
    m_boundsMesh->draw();
    m_drawCallCount++;
}


void Renderer::drawComponents(
    const std::unordered_map<int, std::unique_ptr<ComponentView>>& componentViews
)
{
    // Group by shader so components sharing a shader still get instanced
    // together.
    std::unordered_map<std::string, std::vector<glm::vec2>> positionsByShader;
    std::unordered_map<std::string, glm::vec2> sizeByShader;

    for (const auto& [id, view] : componentViews)
    {
        positionsByShader[view->getShaderName()].push_back(view->getPosition());
        sizeByShader[view->getShaderName()] = view->getSize();
    }

    for (auto& [shaderName, positions] : positionsByShader)
    {
        auto* shader = acquireShader(shaderName);
        if (!shader)
            continue; // acquireShader already logged the missing name

        shader->use();
        shader->setVec2("uPanOffset", m_currentCamera.panOffset.x, m_currentCamera.panOffset.y);
        shader->setFloat("uZoom", m_currentCamera.zoom);
        shader->setFloat("uAspectRatio", m_currentCamera.aspectRatio);
        shader->setVec2("uGateSize", sizeByShader[shaderName].x, sizeByShader[shaderName].y);

        std::vector<float> flatPositions;
        flatPositions.reserve(positions.size() * 2);
        for (const auto& p : positions)
        {
            flatPositions.push_back(p.x);
            flatPositions.push_back(p.y);
        }

        m_gateMesh->setInstanceData(flatPositions, {2}, 1);
        m_gateMesh->drawInstanced(static_cast<unsigned int>(positions.size()));
    }
    m_drawCallCount++;
}


void Renderer::drawPins(
    const std::unordered_map<int, std::unique_ptr<ComponentView>>& componentViews,
    int hoveredCompId,
    int hoveredPinIdx,
    PinType hoveredPinType
)
{
    auto* shader = acquireShader("pin");
    if (!shader)
        return;
    shader->use();
    shader->setVec2("uPanOffset", m_currentCamera.panOffset.x, m_currentCamera.panOffset.y);
    shader->setFloat("uZoom", m_currentCamera.zoom);
    shader->setFloat("uAspectRatio", m_currentCamera.aspectRatio);
    shader->setFloat("uWindowHeight", static_cast<float>(m_currentCamera.windowHeight));

    // World-space diameter: ~half a grid cell (0.05 * 0.48 = 0.024f)
    shader->setFloat("uPointSize", 0.024f);

    std::vector<float> pinInstanceData;
    int totalPins = 0;

    for (const auto& [id, view] : componentViews)
    {
        auto processPin = [&](const PinUI& pin)
        {
            glm::vec2 pinWorldPos = view->getAbsolutePinWorldPos(pin);
            pinInstanceData.push_back(pinWorldPos.x);
            pinInstanceData.push_back(pinWorldPos.y);

            if (id == hoveredCompId && pin.pin_index == static_cast<uint32_t>(hoveredPinIdx) &&
                pin.type == hoveredPinType)
            {
                float r = 255.0f / 255.0f, g = 159.0f / 255.0f, b = 28.0f / 255.0f, a = 1.0f;
                pinInstanceData.insert(pinInstanceData.end(), {r, g, b, a});
            }
            else
            {
                if (pin.state == PinState::DISCONNECTED)
                    pinInstanceData.insert(pinInstanceData.end(), {0.0f, 0.0f, 1.0f, 1.0f});
                else if (pin.state == PinState::ON)
                    pinInstanceData.insert(pinInstanceData.end(), {0.0f, 1.0f, 0.0f, 1.0f});
                else
                    pinInstanceData.insert(pinInstanceData.end(), {1.0f, 0.0f, 0.0f, 1.0f});
            }
            totalPins++;
        };

        for (const auto& pin : view->getInputPins())
            processPin(pin);
        for (const auto& pin : view->getOutputPins())
            processPin(pin);
    }

    if (totalPins > 0)
    {
        m_pointMesh->setInstanceData(pinInstanceData, {2, 4}, 1);
        m_pointMesh->drawInstanced(totalPins);
        m_drawCallCount++;
    }
}


void Renderer::drawComponentBoundingBox(const ComponentView& component, float padding, float alpha)
{
    auto* shader = acquireShader("wire"); // reused: a bounding box is just 4 colored lines
    if (!shader)
        return;
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

    std::vector<float> boxData = {topLeft.x,     topLeft.y,     0.0f, r, g, b, a,
                                  topRight.x,    topRight.y,    0.0f, r, g, b, a,

                                  topRight.x,    topRight.y,    0.0f, r, g, b, a,
                                  bottomRight.x, bottomRight.y, 0.0f, r, g, b, a,

                                  bottomRight.x, bottomRight.y, 0.0f, r, g, b, a,
                                  bottomLeft.x,  bottomLeft.y,  0.0f, r, g, b, a,

                                  bottomLeft.x,  bottomLeft.y,  0.0f, r, g, b, a,
                                  topLeft.x,     topLeft.y,     0.0f, r, g, b, a};

    m_boundsMesh->updateData(boxData, 7);
    m_boundsMesh->draw();
    m_drawCallCount++;
}


void Renderer::drawGridPointHighlight(GridCoords gridPos, float opacity)
{
    auto* shader = acquireShader("pin");
    if (!shader)
        return;
    shader->use();
    shader->setVec2("uPanOffset", m_currentCamera.panOffset.x, m_currentCamera.panOffset.y);
    shader->setFloat("uZoom", m_currentCamera.zoom);
    shader->setFloat("uAspectRatio", m_currentCamera.aspectRatio);
    shader->setFloat("uWindowHeight", static_cast<float>(m_currentCamera.windowHeight));

    // Scale guide point with world space as well
    shader->setFloat("uPointSize", 0.018f);

    glm::vec2 worldPos = GridSystem::gridToWorld(gridPos);
    float r = 255.0f / 255.0f, g = 159.0f / 255.0f, b = 28.0f / 255.0f;

    std::vector<float> data = {worldPos.x, worldPos.y, r, g, b, opacity};
    m_pointMesh->setInstanceData(data, {2, 4}, 1);
    m_pointMesh->drawInstanced(1);
    m_drawCallCount++;
}


void Renderer::drawIntersections(const std::vector<glm::vec3>& intersectionData)
{
    if (intersectionData.empty())
        return;

    auto* shader = acquireShader("pin");
    if (!shader)
        return;
    shader->use();
    shader->setVec2("uPanOffset", m_currentCamera.panOffset.x, m_currentCamera.panOffset.y);
    shader->setFloat("uZoom", m_currentCamera.zoom);
    shader->setFloat("uAspectRatio", m_currentCamera.aspectRatio);
    shader->setFloat("uWindowHeight", static_cast<float>(m_currentCamera.windowHeight));

    // Increased size so the outer dark rim extends past the wire boundaries
    shader->setFloat("uPointSize", 0.028f);

    std::vector<float> instancedData;
    instancedData.reserve(intersectionData.size() * 6);

    for (const auto& data : intersectionData)
    {
        glm::vec2 worldPos =
            GridSystem::gridToWorld({static_cast<int>(data.x), static_cast<int>(data.y)});
        instancedData.push_back(worldPos.x);
        instancedData.push_back(worldPos.y);

        if (data.z == 0.0f)
            instancedData.insert(instancedData.end(), {0.0f, 0.0f, 1.0f, 1.0f});
        else if (data.z == 1.0f)
            instancedData.insert(instancedData.end(), {0.0f, 1.0f, 0.0f, 1.0f});
        else
            instancedData.insert(instancedData.end(), {1.0f, 0.0f, 0.0f, 1.0f});
    }

    m_pointMesh->setInstanceData(instancedData, {2, 4}, 1);
    m_pointMesh->drawInstanced(static_cast<int>(intersectionData.size()));
    m_drawCallCount++;
}


void Renderer::drawLabels(
    const std::unordered_map<int, std::unique_ptr<ComponentView>>& componentViews
)
{
    std::vector<TextVertex> vertices;

    // World units per font pixel; the atlas is baked at 48 px. 0.0012 puts a
    // capital letter at ~0.037 world units (~3/4 of a grid cell): legible at the
    // default zoom without crowding a 4-cell-tall body.
    const float maxLabelScale = 0.0012f;
    const float pinLabelScale = 0.0010f;

    // A body label may occupy at most this fraction of the body's width, so it can
    // never reach the pins on either side.
    const float labelWidthFraction = 0.8f;
    // How far pin names sit inwards of their pins, i.e. just inside the body edge
    // (~0.2 of a grid cell).
    const float pinLabelInset = 0.01f;

    const float pinCapHeight = getCapHeight(pinLabelScale, m_font);

    for (const auto& [id, view] : componentViews)
    {
        // 1. Latches are the only components carrying labels so far.
        auto* lv = dynamic_cast<LatchView*>(view.get());
        if (!lv)
            continue;

        const glm::vec2 pos = lv->getPosition();
        const glm::vec2 size = lv->getSize();

        // 2. Body label, centred on the body. The scale is capped and then shrunk
        // to fit, so a long name such as "SR LATCH" still stays inside a 6-cell-wide
        // body while it and "D LATCH" render at the same size.
        const std::string& label = lv->getLabel();
        float labelScale = maxLabelScale;
        const float naturalWidth = getTextWidth(label, 1.0f, m_font);
        if (naturalWidth > 0.0f)
            labelScale = std::min(maxLabelScale, (size.x * labelWidthFraction) / naturalWidth);

        const float labelWidth = getTextWidth(label, labelScale, m_font);
        buildTextGeometry(
            label,
            pos.x - labelWidth * 0.5f,
            pos.y - getCapHeight(labelScale, m_font) * 0.5f, // centres the caps on the body
            labelScale,
            glm::vec4(1.0f, 1.0f, 1.0f, 0.95f),
            m_font,
            vertices
        );

        // 3. Pin names: inwards of their pin, and centred on the pin's row.
        const auto& inLabels = lv->getInputLabels();
        const auto& inPins = lv->getInputPins();
        for (size_t i = 0; i < inPins.size() && i < inLabels.size(); ++i)
        {
            const glm::vec2 pinPos = lv->getAbsolutePinWorldPos(inPins[i]);
            buildTextGeometry(
                inLabels[i],
                pinPos.x + pinLabelInset,
                pinPos.y - pinCapHeight * 0.5f,
                pinLabelScale,
                glm::vec4(0.75f, 0.85f, 0.95f, 0.85f),
                m_font,
                vertices
            );
        }

        const auto& outLabels = lv->getOutputLabels();
        const auto& outPins = lv->getOutputPins();
        for (size_t i = 0; i < outPins.size() && i < outLabels.size(); ++i)
        {
            const glm::vec2 pinPos = lv->getAbsolutePinWorldPos(outPins[i]);
            // Output names run leftwards from their pin, so the box is anchored on
            // its right edge instead.
            const float pinLabelWidth = getTextWidth(outLabels[i], pinLabelScale, m_font);
            buildTextGeometry(
                outLabels[i],
                pinPos.x - pinLabelInset - pinLabelWidth,
                pinPos.y - pinCapHeight * 0.5f,
                pinLabelScale,
                glm::vec4(0.75f, 0.85f, 0.95f, 0.85f),
                m_font,
                vertices
            );
        }
    }

    if (vertices.empty())
        return;

    // Flatten data for VBO
    std::vector<float> data;
    data.reserve(vertices.size() * 8);
    for (const auto& v : vertices)
    {
        data.push_back(v.pos.x);
        data.push_back(v.pos.y);
        data.push_back(v.uv.x);
        data.push_back(v.uv.y);
        data.push_back(v.color.r);
        data.push_back(v.color.g);
        data.push_back(v.color.b);
        data.push_back(v.color.a);
    }

    auto* shader = acquireShader("text");
    if (!shader)
        return;

    shader->use();

    // Map camera view to ortho matrix
    glm::mat4 proj = glm::ortho(
        (m_currentCamera.panOffset.x - m_currentCamera.aspectRatio / m_currentCamera.zoom),
        (m_currentCamera.panOffset.x + m_currentCamera.aspectRatio / m_currentCamera.zoom),
        (m_currentCamera.panOffset.y - 1.0f / m_currentCamera.zoom),
        (m_currentCamera.panOffset.y + 1.0f / m_currentCamera.zoom),
        -1.0f,
        1.0f
    );
    shader->setMat4("uProjectionView", proj);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_font.textureId);
    shader->setBool("uFontTexture", 0);

    m_textMesh->updateData(data, 8);
    m_textMesh->draw();
    m_drawCallCount++;
}


void Renderer::drawDebugOverlay(const DebugMetrics& metrics)
{
    if (m_currentCamera.windowWidth <= 0 || m_currentCamera.windowHeight <= 0)
        return;

    std::vector<TextVertex> vertices;

    // Line 1: FPS, Frame Time, and Draw Calls
    std::ostringstream ssL1;
    ssL1 << std::fixed << std::setprecision(1) << "FPS: " << metrics.fps << " ("
         << metrics.frameTimeMs << " ms) | Draw Calls: " << metrics.drawCalls;

    // Line 2: Propagation Duration & Latency
    std::ostringstream ssL2;
    ssL2 << std::fixed << std::setprecision(3) << "Propagate Exec: " << metrics.lastPropagateMs
         << " ms (Last Call: ";
    if (metrics.timeSinceLastPropagateMs >= 999.0f)
        ssL2 << ">999 ms ago)";
    else
        ssL2 << std::fixed << std::setprecision(0) << metrics.timeSinceLastPropagateMs
             << " ms ago)";

    // Line 3: Evaluation Order & Topological Status
    std::ostringstream ssL3;
    ssL3 << "Eval Order: " << metrics.evalOrderCount << "/" << metrics.totalComponents
         << " components";
    if (metrics.evalResult == EvalOrderResult::CYCLE_DETECTED)
        ssL3 << " [CYCLE DETECTED]";
    else
        ssL3 << " [OK]";

    // Line 4: Electrical Topology & Short Contention
    std::ostringstream ssL4;
    ssL4 << "Topology: " << metrics.netCount << " Nets | " << metrics.wireCount << " Wires | "
         << metrics.shortedNetCount << " Shorts";

    // Line 5: Interaction / Selection & Cursor Position
    std::ostringstream ssL5;
    if (metrics.selectedCompId != -1)
        ssL5 << "Selected: Comp #" << metrics.selectedCompId;
    else if (metrics.hoveredPinComponentId != -1)
        ssL5 << "Hover: Comp #" << metrics.hoveredPinComponentId << " Pin "
             << (metrics.hoveredPinType == PinType::INPUT ? "In[" : "Out[") << metrics.hoveredPinIdx
             << "]";
    else if (metrics.hoveredCompId != -1)
        ssL5 << "Hover: Comp #" << metrics.hoveredCompId;
    else if (metrics.hoveredWireId != INVALID_WIRE_ID)
        ssL5 << "Hover: Wire #" << metrics.hoveredWireId;
    else
        ssL5 << "Hover: None";

    ssL5 << " | Grid: (" << metrics.cursorGrid.x << ", " << metrics.cursorGrid.y << ")";

    std::vector<std::pair<std::string, glm::vec4>> lines = {
        {"[DEBUG HUD] (F3)", glm::vec4(1.0f, 0.62f, 0.11f, 1.0f)}, // Orange Header
        {ssL1.str(), glm::vec4(0.85f, 0.85f, 0.85f, 0.95f)},
        {ssL2.str(),
         (metrics.timeSinceLastPropagateMs < 50.0f) ? glm::vec4(0.35f, 0.90f, 0.45f, 0.95f)
                                                    : glm::vec4(0.70f, 0.70f, 0.75f, 0.85f)},
        {ssL3.str(),
         (metrics.evalResult == EvalOrderResult::CYCLE_DETECTED)
             ? glm::vec4(1.0f, 0.25f, 0.25f, 1.0f)
             : glm::vec4(0.35f, 0.90f, 0.45f, 0.95f)},
        {ssL4.str(),
         (metrics.shortedNetCount > 0)
             ? glm::vec4(1.0f, 0.25f, 0.25f, 1.0f) // Highlight shorts in red
             : glm::vec4(0.85f, 0.85f, 0.85f, 0.95f)},
        {ssL5.str(), glm::vec4(0.75f, 0.85f, 0.95f, 0.90f)}
    };

    const float screenScale = 0.35f;
    const float lineSpacing = 20.0f;
    const float rightMargin = 16.0f;
    const float startY = 24.0f;

    for (size_t i = 0; i < lines.size(); ++i)
    {
        // 1. Calculate the exact pixel width of this line
        float lineWidthPixels = getTextWidth(lines[i].first, screenScale, m_font);

        // 2. Align to the right screen boundary
        float startX =
            static_cast<float>(m_currentCamera.windowWidth) - rightMargin - lineWidthPixels;

        float curX = startX / screenScale;
        float curY = (startY + i * lineSpacing) / screenScale;

        for (char c : lines[i].first)
        {
            if (c < 32 || c >= 128)
                continue;

            stbtt_aligned_quad q;
            stbtt_GetBakedQuad(
                m_font.cdata, m_font.atlasWidth, m_font.atlasHeight, c - 32, &curX, &curY, &q, 1
            );

            float x0 = q.x0 * screenScale;
            float x1 = q.x1 * screenScale;
            float y0 = q.y0 * screenScale;
            float y1 = q.y1 * screenScale;

            const auto& col = lines[i].second;

            vertices.push_back({{x0, y0}, {q.s0, q.t0}, col});
            vertices.push_back({{x1, y0}, {q.s1, q.t0}, col});
            vertices.push_back({{x1, y1}, {q.s1, q.t1}, col});

            vertices.push_back({{x0, y0}, {q.s0, q.t0}, col});
            vertices.push_back({{x1, y1}, {q.s1, q.t1}, col});
            vertices.push_back({{x0, y1}, {q.s0, q.t1}, col});
        }
    }

    if (vertices.empty())
        return;

    std::vector<float> data;
    data.reserve(vertices.size() * 8);
    for (const auto& v : vertices)
    {
        data.push_back(v.pos.x);
        data.push_back(v.pos.y);
        data.push_back(v.uv.x);
        data.push_back(v.uv.y);
        data.push_back(v.color.r);
        data.push_back(v.color.g);
        data.push_back(v.color.b);
        data.push_back(v.color.a);
    }

    auto* shader = acquireShader("text");
    if (!shader)
        return;

    shader->use();

    glm::mat4 screenProj = glm::ortho(
        0.0f,
        static_cast<float>(m_currentCamera.windowWidth),
        static_cast<float>(m_currentCamera.windowHeight),
        0.0f,
        -1.0f,
        1.0f
    );
    shader->setMat4("uProjectionView", screenProj);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_font.textureId);
    shader->setBool("uFontTexture", 0);

    m_textMesh->updateData(data, 8);
    m_textMesh->draw();
    m_drawCallCount++;
}