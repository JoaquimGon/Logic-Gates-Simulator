#include "Renderer.h"

#include "Components/Definitions/NativeDefinitions.h"
#include "Geometry/GridSystem.h"
#include "Graphics/Presentation/LabelLayout.h"
#include "WireGeometry.h"

#include <algorithm>
#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>
#include <unordered_set>
#include <utility>

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
    m_leadMesh.reset();
    m_boundsMesh.reset();
    m_text.shutdown();

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

bool Renderer::init()
{
    shutdown();
    m_missingShaderWarned.clear();
    bool ready = true;
    auto load = [&](const auto& key, const auto& vertex, const auto& fragment)
    {
        if (!m_sm.load(key, vertex, fragment))
            ready = false;
    };
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
    std::unordered_set<std::string> loadedComponentShaders;
    for (const auto& definition : nativeDefinitions())
    {
        const auto& resources = definition.presentation.shader;
        if (loadedComponentShaders.insert(resources.key).second)
            load(resources.key, resources.vertexPath, resources.fragmentPath);
    }
    const auto& box = boxShaderResources();
    load(box.key, box.vertexPath, box.fragmentPath);

    // Pins
    load("pin", "shaders/components/pins.vert", "shaders/components/pins.frag");

    // Text
    load("text", "shaders/text/text.vert", "shaders/text/text.frag");

    // Grid & Wires
    load("grid", "shaders/vec3Shader.vert", "shaders/grid.frag");
    load("wire", "shaders/wires/wires.vert", "shaders/wires/wires.frag");

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

    m_leadMesh = std::make_unique<Mesh>(
        std::vector<float>{}, std::vector<unsigned int>{}, wireLayout, GL_TRIANGLES
    );

    // ==========================================
    // Bounding Box Mesh
    // ==========================================
    VertexLayout boundsLayout;
    boundsLayout.addAttribute(3); // Position
    boundsLayout.addAttribute(4); // Color
    m_boundsMesh = std::make_unique<Mesh>(
        std::vector<float>{}, std::vector<unsigned int>{}, boundsLayout, GL_TRIANGLES
    );

    ready = m_text.init(PROJECT_FONT_PATH) && ready;
    if (!ready)
    {
        std::cerr << "[Renderer] Required shader/font initialization failed.\n";
        shutdown();
    }
    return ready;
}

void Renderer::beginFrame(const CanvasCameraFrame& camera)
{
    m_sm.checkHotReload();
    m_currentCamera = camera;
    m_screenClip.reset();
    m_drawCallCount = 0; // Reset at the beginning of each frame
    setScreenViewport();
    glClear(GL_COLOR_BUFFER_BIT);
}

void Renderer::setScreenViewport()
{
    if (m_screenClip)
    {
        glEnable(GL_SCISSOR_TEST);
        const auto& clip = *m_screenClip;
        glScissor(clip.x, clip.y, clip.width, clip.height);
    }
    else
        glDisable(GL_SCISSOR_TEST);
    glViewport(
        0,
        0,
        std::max(0, m_currentCamera.surface.framebufferWidth),
        std::max(0, m_currentCamera.surface.framebufferHeight)
    );
}

void Renderer::setScreenClip(std::optional<CanvasViewport> bounds)
{
    m_screenClip.reset();
    if (bounds)
    {
        CanvasCamera clipCamera;
        clipCamera.setViewport(bounds);
        m_screenClip = clipCamera.frame(m_currentCamera.surface).framebufferViewport;
    }
    setScreenViewport();
}

bool Renderer::setCanvasViewport()
{
    if (!m_currentCamera.valid())
        return false;
    const auto& viewport = m_currentCamera.framebufferViewport;
    glViewport(viewport.x, viewport.y, viewport.width, viewport.height);
    glEnable(GL_SCISSOR_TEST);
    glScissor(viewport.x, viewport.y, viewport.width, viewport.height);
    return true;
}

bool Renderer::useCanvasShader(Shader& shader)
{
    if (!setCanvasViewport())
        return false;
    shader.use();
    shader.setMat4("uViewProjection", m_currentCamera.viewProjection);
    // The camera projection stores zoom on Y; UI previews set their scale separately.
    shader.setFloat("uOutlineScale", std::min(m_currentCamera.viewProjection[1][1], 1.0f));
    return true;
}

void Renderer::drawGrid()
{
    auto* shader = acquireShader("grid");
    if (!shader)
        return;
    if (!useCanvasShader(*shader))
        return;
    shader->setMat4("uInverseViewProjection", m_currentCamera.inverseViewProjection);
    shader->setFloat("uGridSpacing", GridSystem::GRID_SPACING);

    m_gridMesh->draw();
    m_drawCallCount++;
}

void Renderer::drawWires(const std::map<WireId, Wire>& wires, const Wire* activeWire)
{
    auto* shader = acquireShader("wire");
    if (!shader)
        return;
    if (!useCanvasShader(*shader))
        return;

    auto& allWiresData = m_wireData;
    allWiresData.clear();

    for (const auto& [id, wire] : wires)
    {
        appendWireVertices(wire, allWiresData);
    }

    if (activeWire != nullptr)
    {
        appendWireVertices(*activeWire, allWiresData);
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
    if (!useCanvasShader(*shader))
        return;

    const auto p1 = GridSystem::gridToWorld(start);
    const auto p2 = GridSystem::gridToWorld(end);
    const auto boxData = buildBoundsVertices(
        {std::min(p1.x, p2.x), std::min(p1.y, p2.y), std::max(p1.x, p2.x), std::max(p1.y, p2.y)},
        padding,
        alpha
    );

    m_boundsMesh->updateData(boxData, 7);
    m_boundsMesh->draw();
    m_drawCallCount++;
}

void Renderer::drawComponents(std::span<const ComponentRenderData> components)
{
    for (const auto& batch : buildComponentBatches(components))
    {
        auto* shader = acquireShader(batch.shader);
        if (!shader)
            continue;
        if (!useCanvasShader(*shader))
            continue;
        shader->setBool("uDrawRearArc", batch.instances.front().drawRearArc);
        m_gateMesh->setInstanceData(packComponentInstances(batch.instances), {2, 2, 4}, 1);
        m_gateMesh->drawInstanced(static_cast<int>(batch.instances.size()));
        ++m_drawCallCount;
    }
}

void Renderer::drawPinLeads(std::span<const ComponentRenderData> components)
{
    auto* shader = acquireShader("wire");
    if (!shader || !useCanvasShader(*shader))
        return;
    auto& data = m_leadData;
    data.clear();
    auto vertex = [&](glm::vec2 p, glm::vec4 color)
    { data.insert(data.end(), {p.x, p.y, 0, color.r, color.g, color.b, color.a}); };
    auto segment = [&](glm::vec2 a, glm::vec2 b, float halfWidth, glm::vec4 color)
    {
        const auto delta = b - a;
        const float length = glm::length(delta);
        if (length < 0.000001f)
            return;
        const glm::vec2 normal(-delta.y / length * halfWidth, delta.x / length * halfWidth);
        vertex(a - normal, color);
        vertex(b - normal, color);
        vertex(b + normal, color);
        vertex(a - normal, color);
        vertex(b + normal, color);
        vertex(a + normal, color);
    };
    for (const auto& component : components)
        if (const auto rail = component.inputRail)
        {
            const auto& tint = component.body.style.tint;
            const glm::vec4 color{tint[0], tint[1], tint[2], tint[3]};
            if (!component.inputArc.empty())
            {
                const float width = component.body.style.inverted ? component.body.size.x / 1.5f
                                                                  : component.body.size.x;
                const float halfWidth = 0.013f * width;
                const float outline = 1.5f * std::min(m_currentCamera.viewProjection[1][1], 1.0f) /
                                      m_currentCamera.pixelsPerWorldUnit;
                // A continuous ribbon with a one-pixel alpha fringe smooths the curve
                // without changing the wire shader or overlapping AA edges at every joint.
                const float fringe = 1.0f / m_currentCamera.pixelsPerWorldUnit;
                auto normal = [&](std::size_t i)
                {
                    const auto previous = i == 0 ? 0 : i - 1;
                    const auto next = std::min(i + 1, component.inputArc.size() - 1);
                    const auto tangent =
                        glm::normalize(component.inputArc[next] - component.inputArc[previous]);
                    return glm::vec2{-tangent.y, tangent.x};
                };
                for (bool border : {true, false})
                {
                    const auto ink = border ? glm::vec4{0.12f, 0.15f, 0.2f, color.a} : color;
                    const float radius = halfWidth + (border ? outline : 0);
                    const float inner = std::max(0.0f, radius - fringe * 0.5f);
                    const float outer = radius + fringe * 0.5f;
                    for (std::size_t i = 1; i < component.inputArc.size(); ++i)
                    {
                        const auto a = component.inputArc[i - 1], b = component.inputArc[i];
                        const auto na = normal(i - 1), nb = normal(i);
                        auto band = [&](float left, float right, float leftAlpha, float rightAlpha)
                        {
                            auto lo = ink, hi = ink;
                            lo.a *= leftAlpha;
                            hi.a *= rightAlpha;
                            vertex(a + na * left, lo);
                            vertex(b + nb * left, lo);
                            vertex(b + nb * right, hi);
                            vertex(a + na * left, lo);
                            vertex(b + nb * right, hi);
                            vertex(a + na * right, hi);
                        };
                        band(-outer, -inner, 0, 1);
                        band(-inner, inner, 1, 1);
                        band(inner, outer, 1, 0);
                    }
                }
            }
            else if (component.body.style.contour == BodyContour::Or)
            {
                // Only extend past the upper/lower back corners; keep the concave middle open.
                const float corner = 0.38f * component.body.size.y;
                segment(
                    {rail->centerX(), rail->bottom},
                    {rail->centerX(), component.body.position.y - corner},
                    rail->width() / 2,
                    color
                );
                segment(
                    {rail->centerX(), component.body.position.y + corner},
                    {rail->centerX(), rail->top},
                    rail->width() / 2,
                    color
                );
            }
            else
                segment(
                    {rail->centerX(), rail->bottom},
                    {rail->centerX(), rail->top},
                    rail->width() / 2,
                    color
                );
        }
    // White terminals go over the extensions; inner XOR rows continue to the solid body.
    for (const auto& component : components)
        for (const auto& pin : component.pins)
            for (std::size_t i = 1; i < pin.lead.size(); ++i)
                segment(pin.lead[i - 1], pin.lead[i], 0.002f, {0.75f, 0.85f, 0.95f, 1});
    if (data.empty())
        return;
    m_leadMesh->updateData(data, 7);
    m_leadMesh->draw();
    ++m_drawCallCount;
}

void Renderer::drawPins(
    std::span<const ComponentRenderData> components,
    int hoveredCompId,
    int hoveredPinIdx,
    PinType hoveredPinType
)
{
    auto* shader = acquireShader("pin");
    if (!shader)
        return;
    if (!useCanvasShader(*shader))
        return;
    shader->setFloat("uPixelsPerWorldUnit", m_currentCamera.pixelsPerWorldUnit);

    // World-space diameter: ~half a grid cell (0.05 * 0.48 = 0.024f)
    shader->setFloat("uPointSize", 0.024f);

    auto& pinInstanceData = m_pinData;
    pinInstanceData.clear();
    int totalPins = 0;

    for (const auto& component : components)
        for (const auto& pin : component.pins)
        {
            pinInstanceData.insert(pinInstanceData.end(), {pin.position.x, pin.position.y});
            if (component.body.id == hoveredCompId && hoveredPinIdx >= 0 &&
                pin.index == static_cast<unsigned int>(hoveredPinIdx) &&
                pin.direction == hoveredPinType)
                pinInstanceData.insert(pinInstanceData.end(), {1, 159.0f / 255, 28.0f / 255, 1});
            else if (pin.state == PinState::DISCONNECTED)
                pinInstanceData.insert(pinInstanceData.end(), {0, 0, 1, 1});
            else if (pin.state == PinState::ON)
                pinInstanceData.insert(pinInstanceData.end(), {0, 1, 0, 1});
            else
                pinInstanceData.insert(pinInstanceData.end(), {1, 0, 0, 1});
            ++totalPins;
        }

    if (totalPins > 0)
    {
        m_pointMesh->setInstanceData(pinInstanceData, {2, 4}, 1);
        m_pointMesh->drawInstanced(totalPins);
        m_drawCallCount++;
    }
}

void Renderer::drawComponentBoundingBox(BodyBounds bounds, float padding, float alpha)
{
    auto* shader = acquireShader("wire");
    if (!shader || !useCanvasShader(*shader))
        return;
    const auto boxData = buildBoundsVertices(bounds, padding, alpha);
    if (boxData.empty())
        return;

    m_boundsMesh->updateData(boxData, 7);
    m_boundsMesh->draw();
    m_drawCallCount++;
}

void Renderer::drawGridPointHighlight(GridCoords gridPos, float opacity)
{
    auto* shader = acquireShader("pin");
    if (!shader)
        return;
    if (!useCanvasShader(*shader))
        return;
    shader->setFloat("uPixelsPerWorldUnit", m_currentCamera.pixelsPerWorldUnit);

    // Guide diameter exceeds wire thickness so its colored core stays visible on the route.
    shader->setFloat("uPointSize", 0.030f);

    glm::vec2 worldPos = GridSystem::gridToWorld(gridPos);
    float r = 255.0f / 255.0f, g = 159.0f / 255.0f, b = 28.0f / 255.0f;

    std::vector<float> data = {worldPos.x, worldPos.y, r, g, b, opacity};
    m_pointMesh->setInstanceData(data, {2, 4}, 1);
    m_pointMesh->drawInstanced(1);
    m_drawCallCount++;
}

void Renderer::drawIntersections(std::span<const glm::vec3> intersectionData)
{
    if (intersectionData.empty())
        return;

    auto* shader = acquireShader("pin");
    if (!shader)
        return;
    if (!useCanvasShader(*shader))
        return;
    shader->setFloat("uPixelsPerWorldUnit", m_currentCamera.pixelsPerWorldUnit);

    // Increased size so the outer dark rim extends past the wire boundaries
    shader->setFloat("uPointSize", 0.034f);

    auto& instancedData = m_intersectionData;
    instancedData.clear();
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

void Renderer::drawWorldRect(BodyBounds bounds, glm::vec4 color)
{
    auto* shader = acquireShader("wire");
    if (!shader || bounds.width() <= 0 || bounds.height() <= 0 || !useCanvasShader(*shader))
        return;
    std::vector<float> vertices;
    for (const auto point :
         {glm::vec2{bounds.left, bounds.bottom},
          glm::vec2{bounds.right, bounds.bottom},
          glm::vec2{bounds.right, bounds.top},
          glm::vec2{bounds.left, bounds.bottom},
          glm::vec2{bounds.right, bounds.top},
          glm::vec2{bounds.left, bounds.top}})
        vertices.insert(vertices.end(), {point.x, point.y, 0, color.r, color.g, color.b, color.a});
    m_boundsMesh->updateData(vertices, 7);
    m_boundsMesh->draw();
    ++m_drawCallCount;
}

void Renderer::drawScreenRect(CanvasViewport bounds, glm::vec4 color)
{
    const auto& surface = m_currentCamera.surface;
    if (bounds.width <= 0 || bounds.height <= 0 || surface.windowWidth <= 0 ||
        surface.windowHeight <= 0 || surface.framebufferWidth <= 0 ||
        surface.framebufferHeight <= 0)
        return;
    auto* shader = acquireShader("wire");
    if (!shader)
        return;
    setScreenViewport();
    shader->use();
    shader->setMat4(
        "uViewProjection",
        glm::ortho(
            0.0f,
            static_cast<float>(surface.windowWidth),
            static_cast<float>(surface.windowHeight),
            0.0f,
            -1.0f,
            1.0f
        )
    );
    std::vector<float> vertices;
    for (const auto point :
         {glm::dvec2{bounds.x, bounds.y},
          glm::dvec2{bounds.x + bounds.width, bounds.y},
          glm::dvec2{bounds.x + bounds.width, bounds.y + bounds.height},
          glm::dvec2{bounds.x, bounds.y},
          glm::dvec2{bounds.x + bounds.width, bounds.y + bounds.height},
          glm::dvec2{bounds.x, bounds.y + bounds.height}})
        vertices.insert(
            vertices.end(),
            {static_cast<float>(point.x),
             static_cast<float>(point.y),
             0,
             color.r,
             color.g,
             color.b,
             color.a}
        );
    m_boundsMesh->updateData(vertices, 7);
    m_boundsMesh->draw();
    ++m_drawCallCount;
}

void Renderer::drawScreenComponent(const ComponentBodyInstance& body)
{
    const auto& surface = m_currentCamera.surface;
    if (body.size.x <= 0 || body.size.y <= 0 || surface.windowWidth <= 0 ||
        surface.windowHeight <= 0 || surface.framebufferWidth <= 0 ||
        surface.framebufferHeight <= 0)
        return;
    auto* shader = acquireShader(body.shader);
    if (!shader)
        return;
    setScreenViewport();
    shader->use();
    shader->setMat4(
        "uViewProjection",
        glm::ortho(
            0.0f,
            static_cast<float>(surface.windowWidth),
            static_cast<float>(surface.windowHeight),
            0.0f,
            -1.0f,
            1.0f
        )
    );
    shader->setFloat("uOutlineScale", 1.0f);
    shader->setBool("uDrawRearArc", true);
    auto instance = body;
    instance.size.y = -instance.size.y; // Preserve the shader's upward local Y in screen space.
    m_gateMesh->setInstanceData(packComponentInstances({&instance, 1}), {2, 2, 4}, 1);
    m_gateMesh->drawInstanced(1);
    ++m_drawCallCount;
}

void Renderer::drawText(std::span<const TextRun> runs, TextSpace space)
{
    auto* shader = acquireShader("text");
    const auto& surface = m_currentCamera.surface;
    if (!shader || surface.windowWidth <= 0 || surface.windowHeight <= 0 ||
        surface.framebufferWidth <= 0 || surface.framebufferHeight <= 0)
        return;
    glm::mat4 projection;
    if (space == TextSpace::World)
    {
        if (!setCanvasViewport())
            return;
        projection = m_currentCamera.viewProjection;
    }
    else
    {
        setScreenViewport();
        projection = glm::ortho(
            0.0f,
            static_cast<float>(surface.windowWidth),
            static_cast<float>(surface.windowHeight),
            0.0f,
            -1.0f,
            1.0f
        );
    }
    m_drawCallCount += m_text.draw(runs, space, projection, *shader);
}

void Renderer::drawLabels(std::span<const ComponentRenderData> components)
{
    drawText(layoutComponentLabels(components, m_text.metrics()), TextSpace::World);
}

void Renderer::drawDebugOverlay(const DebugMetrics& metrics, bool showMetrics)
{
    const auto runs = layoutDebugOverlay(
        metrics,
        showMetrics,
        m_currentCamera.surface.windowWidth,
        m_currentCamera.surface.windowHeight,
        m_text.metrics()
    );
    if (runs.empty())
        return;
    setScreenClip(std::nullopt);
    float left = runs.front().baseline.x;
    float top = runs.front().baseline.y;
    float right = left, bottom = top;
    for (const auto& run : runs)
    {
        const float lineHeight = m_text.metrics().fontHeight * run.scale;
        left = std::min(left, run.baseline.x);
        top = std::min(top, run.baseline.y - lineHeight);
        right =
            std::max(right, run.baseline.x + getTextWidth(run.text, run.scale, m_text.metrics()));
        bottom = std::max(bottom, run.baseline.y + lineHeight * 0.25f);
    }
    constexpr float padding = 8;
    drawScreenRect(
        {left - padding, top - padding, right - left + 2 * padding, bottom - top + 2 * padding},
        {0, 0, 0, 0.5f}
    );
    drawText(runs, TextSpace::Screen);
}

void Renderer::drawCanvas(const CanvasFrame& frame)
{
    // Back-to-front canvas contract. Screen overlays/UI are submitted after this pass.
    drawGrid();
    drawPinLeads(frame.components);
    drawComponents(frame.components);
    drawWires(frame.wires, frame.activeWire);
    drawIntersections(frame.junctions);
    if (frame.selectionBox)
    {
        drawWorldRect(*frame.selectionBox, {0.3f, 0.65f, 0.95f, 0.06f});
        drawComponentBoundingBox(*frame.selectionBox, 0, 0.55f);
    }
    for (const auto& selected : frame.selectedBodies)
        drawComponentBoundingBox(selected.bounds, 0.01f, selected.opacity);
    for (const auto& selected : frame.selectedSegments)
        drawWireSegmentBoundingBox(selected.start, selected.end, 0.01f, selected.opacity);
    if (frame.bodyHighlight)
        drawComponentBoundingBox(frame.bodyHighlight->bounds, 0.01f, frame.bodyHighlight->opacity);
    if (frame.segmentHighlight)
        drawWireSegmentBoundingBox(
            frame.segmentHighlight->start,
            frame.segmentHighlight->end,
            0.01f,
            frame.segmentHighlight->opacity
        );
    drawPins(frame.components, frame.hoveredComponent, frame.hoveredPin, frame.hoveredDirection);
    if (frame.wireStartHighlight)
        drawGridPointHighlight(
            frame.wireStartHighlight->position, frame.wireStartHighlight->opacity
        );
    if (frame.gridHighlight &&
        (!frame.wireStartHighlight ||
         frame.gridHighlight->position != frame.wireStartHighlight->position))
        drawGridPointHighlight(frame.gridHighlight->position, frame.gridHighlight->opacity);
    drawLabels(frame.components);
    setScreenViewport();
}
