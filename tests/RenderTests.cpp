#include "Components/Definitions/NativeDefinitions.h"
#include "Editor/Actions/EditorActions.h"
#include "Editor/CircuitViews.h"
#include "Editor/Input.h"
#include "Editor/Scene.h"
#include "Editor/Subcircuits.h"
#include "Graphics/Presentation/ComponentPresentation.h"
#include "Graphics/Renderer.h"
#include "UI/UI.h"

#include <GLFW/glfw3.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <type_traits>

static_assert(!std::is_copy_constructible_v<Mesh>);
static_assert(!std::is_copy_constructible_v<Shader>);
static_assert(!std::is_copy_constructible_v<FontAtlas>);
static_assert(!std::is_copy_constructible_v<Renderer>);

namespace
{
constexpr int extent = 1024;

void require(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}

std::array<unsigned char, 4> pixel(glm::vec2 world)
{
    std::array<unsigned char, 4> value;
    glReadPixels(
        static_cast<int>((world.x + 1) * extent * 0.5f),
        static_cast<int>((world.y + 1) * extent * 0.5f),
        1,
        1,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        value.data()
    );
    return value;
}

void saveImage(const char* name)
{
    const auto directory = std::filesystem::path(RENDER_ARTIFACTS_DIR);
    std::filesystem::create_directories(directory);
    std::vector<unsigned char> image(extent * extent * 3);
    glReadPixels(0, 0, extent, extent, GL_RGB, GL_UNSIGNED_BYTE, image.data());
    std::ofstream file(directory / name, std::ios::binary);
    file << "P6\n" << extent << ' ' << extent << "\n255\n";
    for (int row = extent - 1; row >= 0; --row)
        file.write(reinterpret_cast<const char*>(image.data() + row * extent * 3), extent * 3);
}

void mixedInstances(Renderer& renderer, const CanvasCameraFrame& camera)
{
    ComponentRenderData a{{1, {-0.4f, 0}, {0.2f, 0.2f}, "box", {}}, "", false, {}};
    ComponentRenderData b = a;
    b.body.id = 2;
    b.body.position = {0.4f, 0};
    b.body.size = {0.4f, 0.6f};
    std::vector<ComponentRenderData> data{b, a};
    renderer.beginFrame(camera);
    renderer.drawComponents(data);
    require(renderer.getDrawCallCount() == 1, "One adjacent shader batch counted multiple calls.");
    require(
        pixel({-0.4f, 0.2f})[2] < 20 && pixel({0.4f, 0.2f})[2] > 40,
        "Same-shader instances rendered with a shared height."
    );
    require(
        pixel({-0.55f, 0})[2] < 20 && pixel({0.55f, 0})[2] > 40,
        "Same-shader instances rendered with a shared width."
    );
    saveImage("mixed-sizes.ppm");
    a.body.style.tint = {1, 0, 0, 1};
    b.body.style.tint = {0, 1, 0, 1};
    data = {a, b};
    renderer.beginFrame(camera);
    renderer.drawComponents(data);
    const auto red = pixel({-0.4f, 0}), green = pixel({0.4f, 0});
    require(
        renderer.getDrawCallCount() == 1 && red[0] > red[1] && green[1] > green[0],
        "Same-shader instances share tint instead of carrying it independently."
    );


    a.body.position = b.body.position = {0, 0};
    a.body.size = b.body.size = {0.6f, 0.6f};
    a.body.style.tint = {1, 0, 0, 0.5f};
    b.body.style.tint = {0, 1, 0, 0.5f};
    // IDs 1(A),2(B),3(A): equal shader names must not reorder the last translucent body.
    b.body.shader = "latch";
    auto c = a;
    c.body.id = 3;
    data = {c, b, a};
    renderer.beginFrame(camera);
    renderer.drawComponents(data);
    auto expected = pixel({0, 0});
    require(
        renderer.getDrawCallCount() == 3 && expected[0] > expected[1],
        "Shader regrouping changed alpha compositing order or draw metrics."
    );
    std::reverse(data.begin(), data.end());
    renderer.beginFrame(camera);
    renderer.drawComponents(data);
    require(pixel({0, 0}) == expected, "Container order changed rendered pixels.");
    renderer.beginFrame(camera);
    renderer.drawComponents({});
    require(renderer.getDrawCallCount() == 0, "Empty bodies counted a draw.");
}

void nativeContacts(Renderer& renderer, const CanvasCameraFrame& camera)
{
    Scene scene;
    for (const auto& definition : nativeDefinitions())
    {
        const std::vector<int> counts =
            definition.pinLayoutRule == PinLayoutRule::SymmetricGateInputs
                ? std::vector<int>{2, 4, 6, 50}
                : std::vector<int>{0};
        for (int count : counts)
        {
            ComponentOverrides options;
            if (count)
                options.inputCount = count;
            const int id = scene.addComponent(
                definition.identity.id, {0, 0}, options, PlacementPolicy::AllowOverlap
            );
            auto data = buildComponentPresentation(scene.getComponentViewMap());
            auto it = std::find_if(
                data.begin(), data.end(), [=](const auto& item) { return item.body.id == id; }
            );
            // Rescale the complete presentation to make every silhouette readable in the
            // framebuffer.
            auto body = *it;
            // Normalize both axes so even 50-input bodies have enough pixels to inspect contact
            // edges. This also exercises the supported affine/aspect-change presentation contract.
            const glm::vec2 factor = glm::vec2(0.7f) / body.body.size;
            body.body.size *= factor;
            for (auto& pin : body.pins)
            {
                pin.position *= factor;
                for (auto& point : pin.lead)
                    point *= factor;
            }
            std::vector<ComponentRenderData> one{body};
            renderer.beginFrame(camera);
            renderer.drawComponents(one);
            for (const auto& pin : body.pins)
            {
                const auto contact = pin.lead.front();
                // A small interior offset tolerates rasterization/AA while exposing CPU/GLSL drift.
                const auto probe = contact * 0.99f;
                const auto value = pixel(probe);
                if (!(value[0] > 20 || value[1] > 20 || value[2] > 25))
                {
                    std::cerr << "Contact: " << definition.identity.id << " inputs=" << count
                              << " pin=" << pin.index
                              << " direction=" << static_cast<int>(pin.direction)
                              << " position=" << contact.x << ',' << contact.y
                              << " pixel=" << static_cast<int>(value[0]) << ','
                              << static_cast<int>(value[1]) << ',' << static_cast<int>(value[2])
                              << '\n';
                    saveImage("contact-failure.ppm");
                    throw std::runtime_error(
                        "Declared silhouette contact does not reach the rendered body."
                    );
                }
            }
            require(glGetError() == GL_NO_ERROR, "OpenGL rejected the instance-data contract.");
        }
    }
}

void editedGate(Renderer& renderer, const CanvasCameraFrame& camera)
{
    Scene scene;
    const int id = scene.addComponent(BuiltinComponentIds::And, {0, 0});
    auto draw = [&]
    {
        auto body = buildComponentPresentation(scene.getComponentViewMap()).front();
        const glm::vec2 factor = glm::vec2(0.7f) / body.body.size;
        body.body.size *= factor;
        for (auto& pin : body.pins)
        {
            pin.position *= factor;
            for (auto& point : pin.lead)
                point *= factor;
        }
        renderer.beginFrame(camera);
        const std::vector<ComponentRenderData> one{body};
        renderer.drawComponents(one);
        return body;
    };
    draw();
    const auto plain = pixel({0.4f * 0.7f, 0.2f * 0.7f});
    const auto result = EditorActions(scene).apply({ConfigureComponentProperties{
        .componentId = id, .label = "Learning", .inputCount = 4, .inverted = true
    }});
    require(static_cast<bool>(result), "Native gate edit failed.");
    const auto inverted = draw();
    const auto gap = pixel({0.4f * 0.7f, 0.2f * 0.7f});
    const auto bubble = pixel({0.41f * 0.7f, 0});
    require(
        plain[2] > 100 && gap[2] < 40 && bubble[2] > 100,
        "Inversion edit did not select the matching native silhouette/bubble."
    );
    for (const auto& pin : inverted.pins)
    {
        const auto contact = pixel(pin.lead.front() * 0.99f);
        require(contact[2] > 25, "Edited inversion lead misses the rendered body.");
    }
    saveImage("edited-gate-inverted.ppm");
}

void preview(Renderer& renderer, const CanvasCameraFrame& camera)
{
    Scene scene;
    int index = 0;
    for (const auto& definition : nativeDefinitions())
    {
        const int column = index % 4, row = index / 4;
        ComponentOverrides options;
        if (definition.pinLayoutRule == PinLayoutRule::SymmetricGateInputs)
            options.inputCount = 4;
        scene.addComponent(definition.identity.id, {-14 + column * 9, 13 - row * 10}, options);
        ++index;
    }
    ComponentDefinition custom;
    custom.identity = {"custom.preview", 1};
    custom.displayName = "Custom";
    custom.behavior = NOT;
    custom.presentation.shader = boxShaderResources();
    custom.presentation.bodyLabel = "CUSTOM BOX";
    custom.presentation.showPinLabels = true;
    custom.presentation.body.tint = {0.7f, 1, 0.9f, 1};
    custom.layout = {
        0.35f,
        0.3f,
        {{"in", "Data", PinType::INPUT, 0, {-4, 0}, {{-3, 1}, {-4, 1}, {-4, 0}}},
         {"out", "Result", PinType::OUTPUT, 0, {4, 0}, {}}}
    };
    auto added = EditorActions(scene).apply(
        {RegisterComponentDefinition{custom}, CreateComponent{custom.identity.id, {13, -15}}}
    );
    require(static_cast<bool>(added), "Custom visual preview could not be created.");
    const auto components = buildComponentPresentation(scene.getComponentViewMap());
    const auto junctions = scene.getWireIntersections();
    CanvasFrame frame{components, scene.getWires(), junctions};
    frame.bodyHighlight = BodyHighlight{components.back().body.getBodyBounds(), 1};
    renderer.beginFrame(camera);
    renderer.drawCanvas(frame);
    DebugMetrics metrics{};
    metrics.evalResult = SimulationResult::CONNECTION_REJECTED;
    renderer.drawDebugOverlay(metrics, false);
    require(glGetError() == GL_NO_ERROR, "Canvas/text pass produced an OpenGL error.");
    saveImage("component-presentations.ppm");
}

std::array<unsigned char, 4> cameraPixel(const CanvasCameraFrame& frame, glm::vec2 world)
{
    const auto point = frame.worldToWindow(world);
    require(point.has_value(), "Cannot sample an inactive camera.");
    const int x =
        static_cast<int>(point->x * frame.surface.framebufferWidth / frame.surface.windowWidth);
    const int y =
        frame.surface.framebufferHeight - 1 -
        static_cast<int>(point->y * frame.surface.framebufferHeight / frame.surface.windowHeight);
    std::array<unsigned char, 4> result;
    glReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, result.data());
    return result;
}

void componentOutlines(Renderer& renderer)
{
    auto slateRim = [](const auto& color)
    {
        return std::abs(color[0] - 31) < 15 && std::abs(color[1] - 38) < 15 &&
               std::abs(color[2] - 51) < 15;
    };
    Scene scene;
    scene.addComponent(BuiltinComponentIds::And, {0, 0});
    const auto base = buildComponentPresentation(scene.getComponentViewMap()).front();
    for (float zoom : {1.0f, 2.0f})
    {
        CanvasCamera camera;
        camera.setZoom(zoom);
        const auto frame = camera.frame({extent, extent, extent, extent});
        const float onePixel = 1.0f / frame.pixelsPerWorldUnit;
        auto body = base;
        body.body.size = {0.6f, 0.6f};
        renderer.beginFrame(frame);
        renderer.drawComponents(std::vector<ComponentRenderData>{body});
        require(
            slateRim(cameraPixel(frame, {0, 0.252f - 0.5f * onePixel})),
            "Basic gate has no thin outline along its perimeter."
        );
        require(
            cameraPixel(frame, {0, 0.252f - 4 * onePixel})[2] > 180,
            "Component outline expanded with zoom or covered its fill."
        );
        require(
            cameraPixel(frame, {0.048f, 0})[2] > 180,
            "AND outline exposed an internal seam between its box and cap."
        );
        for (auto id :
             {BuiltinComponentIds::Nand, BuiltinComponentIds::Nor, BuiltinComponentIds::Nxor})
        {
            const auto* definition = scene.getComponentCatalog().find(id);
            body.body.style = definition->presentation.body;
            body.body.shader = definition->presentation.shader.key;
            renderer.beginFrame(frame);
            renderer.drawComponents(std::vector<ComponentRenderData>{body});
            require(
                slateRim(cameraPixel(frame, {0.246f, 0.078f - 0.5f * onePixel})),
                "Inversion bubble is missing its outline."
            );
            require(
                slateRim(cameraPixel(frame, {0.194f + 0.5f * onePixel, 0})),
                "Bubble outline disappeared where it meets the gate body."
            );
            const auto center = cameraPixel(frame, {0.246f, 0});
            require(
                std::max({center[0], center[1], center[2]}) > 180,
                "Inversion bubble lost its colored interior."
            );
            if (id == BuiltinComponentIds::Nand && zoom == 1)
                saveImage("component-outline-nand.ppm");
        }
        for (auto id : {BuiltinComponentIds::Xor, BuiltinComponentIds::Nxor})
        {
            const auto* definition = scene.getComponentCatalog().find(id);
            body.body.style = definition->presentation.body;
            body.body.shader = definition->presentation.shader.key;
            renderer.beginFrame(frame);
            renderer.drawComponents(std::vector<ComponentRenderData>{body});
            const float arcX = id == BuiltinComponentIds::Xor ? -0.246f : -0.164f;
            const auto arc = cameraPixel(frame, {arcX, 0});
            for (int channel = 0; channel < 3; ++channel)
                require(
                    std::abs(arc[channel] - body.body.style.tint[channel] * 255) < 20,
                    "XOR rear arc lost its gate color and visibility."
                );
            const float arcEdgeX = id == BuiltinComponentIds::Xor ? -0.2382f : -0.1588f;
            const auto arcRimProbe = cameraPixel(frame, {arcEdgeX + 1.25f * onePixel, 0});
            require(
                std::abs(arcRimProbe[0] - 31) < 20 && std::abs(arcRimProbe[1] - 38) < 20 &&
                    std::abs(arcRimProbe[2] - 51) < 20,
                "XOR rear arc is missing its outer outline."
            );
            require(
                cameraPixel(frame, {-0.3f - onePixel, 0.18f})[2] < 25,
                "XOR rear arc outline escaped its shader quad."
            );
            if (zoom == 1)
                saveImage(
                    id == BuiltinComponentIds::Xor ? "component-outline-xor.ppm"
                                                   : "component-outline-nxor.ppm"
                );
        }
    }
    int normalZoomInsetBlue = 0;
    for (float zoom : {1.0f, 0.5f, 0.25f})
    {
        CanvasCamera camera;
        camera.setZoom(zoom);
        const auto frame = camera.frame({extent, extent, extent, extent});
        auto body = base;
        body.body.size = {0.6f, 0.6f};
        body.body.position.y = -0.002f; // Align the top edge at y=0.25 with pixel boundaries.
        renderer.beginFrame(frame);
        renderer.drawComponents(std::vector<ComponentRenderData>{body});
        const auto inset = cameraPixel(frame, {0, 0.25f - 1.5f / frame.pixelsPerWorldUnit});
        if (zoom == 1)
            normalZoomInsetBlue = inset[2];
        else
            require(
                inset[2] > normalZoomInsetBlue + 60,
                "Zooming out kept a full-width outline instead of preserving the colored fill."
            );
        if (zoom == 0.25f)
            saveImage("component-outline-zoomed-out.ppm");
    }
}

void palettePresentation(Renderer& renderer)
{
    Scene scene;
    Input input;
    input.setScene(&scene);
    UI ui;
    const CanvasSurface surface{extent, extent, extent, extent};
    ui.layout(scene.getComponentCatalog(), surface, input);
    CanvasCamera camera;
    camera.setViewport(input.getCanvasViewport());
    const auto frame = camera.frame(surface);
    int i = 0;
    for (const auto& button : ui.buttons())
    {
        scene.addComponent(button.definitionId, {-8 + (i % 3) * 8, 8 - (i / 3) * 8});
        ++i;
    }
    const auto components = buildComponentPresentation(scene.getComponentViewMap());
    const auto junctions = scene.getWireIntersections();
    renderer.beginFrame(frame);
    renderer.drawCanvas({components, scene.getWires(), junctions});
    ui.draw(renderer, scene, frame);
    std::array<unsigned char, 4> left{}, buttonPixel{};
    glReadPixels(5, extent - 501, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, left.data());
    const auto first = ui.buttons().front().bounds;
    glReadPixels(
        static_cast<int>(first.x + 3),
        extent - static_cast<int>(first.y + 3) - 1,
        1,
        1,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        buttonPixel.data()
    );
    require(
        left[2] > left[0] && buttonPixel[2] > left[2],
        "Palette background/buttons were not drawn in screen space."
    );
    GLint viewport[4];
    glGetIntegerv(GL_VIEWPORT, viewport);
    require(
        glIsEnabled(GL_SCISSOR_TEST) == GL_FALSE && viewport[2] == extent &&
            viewport[3] == extent && glGetError() == GL_NO_ERROR,
        "UI drawing left canvas clipping or invalid GPU state."
    );
    saveImage("component-palette.ppm");

    auto screenPixel = [](glm::dvec2 point, double scale = 1)
    {
        std::array<unsigned char, 4> value{};
        glReadPixels(
            static_cast<int>(point.x * scale),
            extent - static_cast<int>(point.y * scale) - 1,
            1,
            1,
            GL_RGBA,
            GL_UNSIGNED_BYTE,
            value.data()
        );
        return value;
    };
    const auto andButton = std::find_if(
        ui.buttons().begin(),
        ui.buttons().end(),
        [](const auto& item) { return item.definitionId == BuiltinComponentIds::And; }
    );
    require(andButton != ui.buttons().end(), "AND card is missing.");
    const auto andCard = andButton->bounds;
    const glm::dvec2 andCenter{andCard.x + andCard.width / 2, andCard.y + 34};
    const auto blueGate = screenPixel(andCenter);
    require(
        blueGate[2] > 150 && blueGate[2] > blueGate[0] * 2,
        "Native AND card did not reuse its colored component shader."
    );
    for (const auto& button : ui.buttons())
    {
        const auto* definition = scene.getComponentCatalog().find(button.definitionId);
        if (!std::holds_alternative<GateType>(definition->behavior))
            continue;
        const auto& rect = button.bounds;
        int minX = extent, minY = extent, maxX = -1, maxY = -1;
        for (int y = static_cast<int>(rect.y + 8); y < rect.y + rect.height - 28; ++y)
            for (int x = static_cast<int>(rect.x + 8); x < rect.x + rect.width - 8; ++x)
            {
                const auto value = screenPixel({x, y});
                const auto tint = definition->presentation.body.tint;
                const bool fill = std::abs(value[0] - tint[0] * 255) < 8 &&
                                  std::abs(value[1] - tint[1] * 255) < 8 &&
                                  std::abs(value[2] - tint[2] * 255) < 8;
                const bool outline = std::abs(value[0] - 0.12f * 255) < 4 &&
                                     std::abs(value[1] - 0.15f * 255) < 4 &&
                                     std::abs(value[2] - 0.20f * 255) < 4;
                if (fill || outline)
                {
                    minX = std::min(minX, x);
                    maxX = std::max(maxX, x);
                    minY = std::min(minY, y);
                    maxY = std::max(maxY, y);
                }
            }
        require(
            maxX >= minX && std::abs((minX + maxX + 1) * 0.5 - (rect.x + rect.width * 0.5)) <= 2 &&
                std::abs((minY + maxY + 1) * 0.5 - (rect.y + 8 + (rect.height - 36) * 0.5)) <= 2,
            "Native card did not center its visible shape and bubble."
        );
    }
    camera.setCenter({0.4f, -0.2f});
    camera.setZoom(2);
    auto moved = camera.frame(surface);
    renderer.beginFrame(moved);
    renderer.drawCanvas({components, scene.getWires(), junctions});
    ui.draw(renderer, scene, moved);
    require(screenPixel(andCenter) == blueGate, "Canvas camera transformed a native card preview.");
    const CanvasSurface dpiSurface{extent / 2, extent / 2, extent, extent};
    ui.layout(scene.getComponentCatalog(), dpiSurface, input);
    camera.setViewport(input.getCanvasViewport());
    auto dpi = camera.frame(dpiSurface);
    renderer.beginFrame(dpi);
    renderer.drawCanvas({components, scene.getWires(), junctions});
    ui.draw(renderer, scene, dpi);
    require(
        screenPixel(andCenter, 2) == blueGate, "Native shader preview ignored framebuffer scaling."
    );
    saveImage("component-palette-dpi.ppm");
    ui.handleInput({UiInputKind::Cursor, 0, 0, 0, 0, 30, 150}, scene, input, dpi);
    ui.handleInput({UiInputKind::Scroll, 0, 0, 0, 0, 0, -20}, scene, input, dpi);
    const auto partial = std::find_if(
        ui.buttons().begin(),
        ui.buttons().end(),
        [](const auto& item)
        { return item.bounds.y < 142 && item.bounds.y + item.bounds.height > 142; }
    );
    require(partial != ui.buttons().end(), "Framebuffer scroll fixture has no partial card.");
    renderer.beginFrame(dpi);
    renderer.drawCanvas({components, scene.getWires(), junctions});
    ui.draw(renderer, scene, dpi);
    const double edgeX = partial->bounds.x + 3;
    const auto cardEdge = screenPixel({edgeX, 145}, 2);
    const auto panelBackground = screenPixel({5, 145}, 2);
    require(
        cardEdge[2] > panelBackground[2] && screenPixel({edgeX, 140}, 2) == panelBackground &&
            screenPixel({edgeX, dpiSurface.windowHeight - 45.0}, 2) == panelBackground &&
            glIsEnabled(GL_SCISSOR_TEST) == GL_FALSE,
        "Scrolling hid partial cards, leaked into header/footer, or retained UI clipping."
    );
    saveImage("component-palette-scrolled.ppm");
    ui.layout(scene.getComponentCatalog(), surface, input);

    const auto b = ui.buttons().front().bounds;
    ui.handleInput(
        {UiInputKind::MouseButton, GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS, 0, 0, b.x + 20, b.y + 15},
        scene,
        input,
        frame
    );
    ui.handleInput({UiInputKind::Cursor, 0, 0, 0, 0, 800, 740}, scene, input, frame);
    renderer.beginFrame(frame);
    renderer.drawCanvas({components, scene.getWires(), junctions});
    ui.draw(renderer, scene, frame);
    require(glGetError() == GL_NO_ERROR, "Palette drag preview produced an OpenGL error.");
    saveImage("component-palette-drag.ppm");
    ui.cancel(input);

    const auto customTab = ui.tabBounds(UI::Tab::Custom);
    ui.handleInput(
        {UiInputKind::MouseButton,
         GLFW_MOUSE_BUTTON_LEFT,
         GLFW_PRESS,
         0,
         0,
         customTab.x + 10,
         customTab.y + 10},
        scene,
        input,
        frame
    );
    require(ui.buttons().empty(), "Custom palette rendered placeholder/native entries.");
    renderer.beginFrame(frame);
    renderer.drawCanvas({components, scene.getWires(), junctions});
    ui.draw(renderer, scene, frame);
    saveImage("component-palette-custom-empty.ppm");

    auto custom = *scene.getComponentCatalog().find(BuiltinComponentIds::DLatch);
    custom.identity = {"test.custom-memory", 1};
    custom.displayName = "Custom memory";
    custom.presentation.kind = PresentationKind::Box;
    custom.presentation.shader = boxShaderResources();
    require(
        static_cast<bool>(EditorActions(scene).apply({RegisterComponentDefinition{custom}})),
        "Cannot register custom list rendering fixture."
    );
    ui.layout(scene.getComponentCatalog(), surface, input);
    renderer.beginFrame(frame);
    renderer.drawCanvas({components, scene.getWires(), junctions});
    ui.draw(renderer, scene, frame);
    require(
        ui.buttons().size() == 1 && ui.buttons()[0].inputCount == 2 &&
            ui.buttons()[0].outputCount == 2 && glGetError() == GL_NO_ERROR,
        "Custom palette lost its name/count row or corrupted GPU state."
    );
    saveImage("component-palette-custom-list.ppm");
}

void outputPresentation(Renderer& renderer)
{
    Scene scene;
    Input input;
    input.setScene(&scene);
    UI ui;
    const CanvasSurface surface{extent, extent, extent, extent};
    ui.layout(scene.getComponentCatalog(), surface, input);
    CanvasCamera camera;
    camera.setViewport(input.getCanvasViewport());
    const auto frame = camera.frame(surface);
    const int source = scene.addComponent(BuiltinComponentIds::Input, {-8, 0});
    const int output = scene.addComponent(BuiltinComponentIds::Output, {8, 0});
    require(
        static_cast<bool>(EditorActions(scene).apply(
            {AddWire{{{-7, 0}, {7, 0}}},
             ConfigureComponentProperties{.componentId = source, .label = "Data"},
             ConfigureComponentProperties{.componentId = output, .label = "Sum"}}
        )),
        "Output presentation fixture failed."
    );
    auto draw = [&]
    {
        scene.propagate();
        scene.syncVisuals();
        const auto components = buildComponentPresentation(scene.getComponentViewMap());
        const auto junctions = scene.getWireIntersections();
        renderer.beginFrame(frame);
        renderer.drawCanvas({components, scene.getWires(), junctions});
        ui.draw(renderer, scene, frame);
        return cameraPixel(frame, {0.4f, 0});
    };
    const auto low = draw();
    saveImage("output-bulb-off.ppm");
    scene.handleClick(source);
    const auto high = draw();
    require(
        high[1] > 200 && high[1] > low[1] + 100 && high[1] > high[0] * 2 && high[1] > high[2] * 2,
        "Output bulb did not change from dark to green with its incoming signal."
    );
    saveImage("output-bulb-on.ppm");
    const auto point = *frame.worldToWindow({0.4f, 0});
    ui.handleInput(
        {UiInputKind::MouseButton, GLFW_MOUSE_BUTTON_RIGHT, GLFW_PRESS, 0, 0, point.x, point.y},
        scene,
        input,
        frame
    );
    ui.handleInput(
        {UiInputKind::MouseButton, GLFW_MOUSE_BUTTON_RIGHT, GLFW_RELEASE, 0, 0, point.x, point.y},
        scene,
        input,
        frame
    );
    const auto field = ui.nameBounds(scene);
    ui.handleInput(
        {UiInputKind::MouseButton,
         GLFW_MOUSE_BUTTON_LEFT,
         GLFW_PRESS,
         0,
         0,
         field.x + 12,
         field.y + 12},
        scene,
        input,
        frame
    );
    draw();
    require(
        input.getUiCapture().keyboard && glGetError() == GL_NO_ERROR,
        "Naming popup failed to draw its focused text field."
    );
    saveImage("input-output-naming.ppm");
    ui.handleInput({UiInputKind::Key, GLFW_KEY_ESCAPE, GLFW_PRESS}, scene, input, frame);
    scene.addComponent(BuiltinComponentIds::Not, {0, 8});
    require(
        static_cast<bool>(
            EditorActions(scene).apply({AddWire{{{1, 8}, {1, 12}, {-2, 12}, {-2, 8}}}})
        ),
        "Oscillation fixture failed."
    );
    const auto unavailable = draw();
    require(
        scene.getLastEvalResult() == SimulationResult::NON_CONVERGENT &&
            unavailable[2] > unavailable[0] && unavailable[1] < high[1],
        "Unavailable output retained the lit high-state appearance."
    );
    saveImage("output-bulb-unavailable.ppm");
}

void wireMarkers(Renderer& renderer, const CanvasCameraFrame& camera)
{
    Wire active;
    active.setPath({{-8, 0}, {8, 0}});
    active.setState(PinState::ON);
    ComponentRenderData source{
        {1, {-0.5f, 0}, {0.1f, 0.1f}, "box", {}},
        "",
        false,
        {{PinType::OUTPUT, 0, PinState::ON, {-0.4f, 0}, "", {}}}
    };
    const std::vector<ComponentRenderData> components{source};
    const std::map<WireId, Wire> wires;
    const std::vector<glm::vec3> junctions;
    CanvasFrame frame{components, wires, junctions, &active};
    frame.wireStartHighlight = GridHighlight{{-8, 0}, 0.9f};
    frame.gridHighlight = GridHighlight{{8, 0}, 1};
    renderer.beginFrame(camera);
    renderer.drawCanvas(frame);
    require(pixel({-0.4f, 0})[0] > 200, "Wire origin marker was obscured by the originating pin.");
    require(
        pixel({0.4f, 0.008f})[0] > 150,
        "Wire endpoint marker did not extend visibly past the wire thickness."
    );
    saveImage("wire-drag-markers.ppm");
    frame.wireStartHighlight.reset();
    frame.gridHighlight = GridHighlight{{0, 0}, 0.55f};
    renderer.beginFrame(camera);
    renderer.drawCanvas(frame);
    const auto hover = pixel({0, 0});
    require(hover[0] > 120 && hover[0] < 170, "Wire hover marker opacity is incorrect.");
    saveImage("wire-hover-marker.ppm");
    frame.gridHighlight.reset();
    renderer.beginFrame(camera);
    renderer.drawCanvas(frame);
    require(
        pixel({0, 0})[0] < 20 && pixel({-0.4f, 0})[0] < 20,
        "Cleared creation markers left orange points on the wire."
    );
    saveImage("wire-markers-cleared.ppm");
}

void roundedBounds(Renderer& renderer, const CanvasCameraFrame& camera)
{
    renderer.beginFrame(camera);
    const BodyBounds bounds{-0.32f, -0.24f, 0.27f, 0.24f};
    renderer.drawComponentBoundingBox(bounds, 0, 1);
    require(pixel({0, bounds.top})[0] > 200, "Outline top stroke is missing.");
    require(
        pixel({0, bounds.top - 0.01f})[0] < 50,
        "Outline is as thick as a wire or fills its interior."
    );
    require(
        pixel({bounds.left, bounds.top})[0] < 50,
        "Outline corner stayed square instead of rounding."
    );
    require(pixel({0, 0})[0] < 50, "Rounded outline filled the component body.");
    const auto corner = pixel({bounds.left + 0.0044f, bounds.top - 0.0044f});
    require(corner[0] > 200, "Rounded corner triangles left a gap.");
    renderer.drawWireSegmentBoundingBox({-4, -2}, {4, -2}, 0.01f, 0.6f);
    require(glGetError() == GL_NO_ERROR, "Triangle outlines produced an OpenGL error.");
    saveImage("rounded-component-bounds.ppm");
}

void componentInformationPresentation(Renderer& renderer)
{
    Scene scene;
    Input input;
    input.setScene(&scene);
    UI ui;
    const CanvasSurface surface{extent, extent, extent, extent};
    ui.layout(scene.getComponentCatalog(), surface, input);
    CanvasCamera camera;
    camera.setViewport(input.getCanvasViewport());
    const auto frame = camera.frame(surface);
    const int latch = scene.addComponent(BuiltinComponentIds::SrLatch, {0, 0});
    ComponentOverrides options;
    options.inputState = true;
    scene.addComponent(BuiltinComponentIds::Input, {-8, 0}, options);
    Wire wire;
    wire.setPath({{-7, 0}, {-5, 0}, {-5, 1}, {-3, 1}});
    scene.commitWire(std::move(wire));
    scene.propagate();
    scene.syncVisuals();
    const auto point = *frame.worldToWindow({0, 0});
    ui.handleInput(
        {UiInputKind::MouseButton, GLFW_MOUSE_BUTTON_RIGHT, GLFW_PRESS, 0, 0, point.x, point.y},
        scene,
        input,
        frame
    );
    ui.handleInput(
        {UiInputKind::MouseButton, GLFW_MOUSE_BUTTON_RIGHT, GLFW_RELEASE, 0, 0, point.x, point.y},
        scene,
        input,
        frame
    );
    require(ui.infoComponentId() == latch, "Cannot open information rendering fixture.");
    const auto components = buildComponentPresentation(scene.getComponentViewMap());
    const auto junctions = scene.getWireIntersections();
    renderer.beginFrame(frame);
    renderer.drawCanvas({components, scene.getWires(), junctions});
    ui.draw(renderer, scene, frame);
    const auto bounds = ui.infoBounds(scene);
    const int width = static_cast<int>(bounds.width), height = static_cast<int>(bounds.height);
    std::vector<unsigned char> pixels(width * height * 4);
    glReadPixels(
        static_cast<int>(bounds.x),
        extent - static_cast<int>(bounds.y + bounds.height),
        width,
        height,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        pixels.data()
    );
    const int sample = ((height - 6) * width + 4) * 4;
    require(
        std::abs(pixels[sample] - 20) <= 2 && std::abs(pixels[sample + 2] - 43) <= 2,
        "Popup background failed to cover the canvas in screen space."
    );
    int glyphPixels = 0;
    for (std::size_t i = 0; i < pixels.size(); i += 4)
        glyphPixels += pixels[i] > 150 && pixels[i + 1] > 150 && pixels[i + 2] > 150;
    require(
        glyphPixels > 200 && glGetError() == GL_NO_ERROR,
        "Popup omitted its text or produced an OpenGL error."
    );
    saveImage("component-information.ppm");
}

void canvasViewport(Renderer& renderer)
{
    CanvasCamera camera;
    camera.setViewport(CanvasViewport{100, 80, 350, 280});
    camera.setCenter({0.25f, -0.15f});
    camera.setZoom(2);
    const auto frame = camera.frame({extent / 2, extent / 2, extent, extent});
    renderer.beginFrame(frame);
    renderer.drawGrid();
    const auto line = cameraPixel(frame, camera.center());
    const auto cell = cameraPixel(frame, camera.center() + glm::vec2{0.025f});
    require(line[0] > cell[0] + 10, "Procedural grid does not follow the shared inverse matrix.");

    ComponentRenderData component{
        {1, camera.center(), {0.2f, 0.15f}, "box", {}},
        "BOX",
        false,
        {{PinType::OUTPUT, 0, PinState::ON, {0.6f, -0.15f}, "", {{0.35f, -0.15f}, {0.6f, -0.15f}}}}
    };
    const std::vector<ComponentRenderData> components{component};
    Wire wire;
    wire.setPath({{-30, 2}, {30, 2}}); // Crosses both viewport edges.
    wire.setState(PinState::ON);
    const std::map<WireId, Wire> wires{{1, wire}};
    const std::vector<glm::vec3> junctions{{0, 2, 0}};
    CanvasFrame canvas{components, wires, junctions};
    canvas.bodyHighlight = BodyHighlight{component.body.getBodyBounds(), 1};
    canvas.segmentHighlight = SegmentHighlight{{-30, 2}, {30, 2}, 1};
    canvas.gridHighlight = GridHighlight{{0, -8}, 1};
    renderer.beginFrame(frame);
    renderer.drawCanvas(canvas);
    GLint restored[4];
    glGetIntegerv(GL_VIEWPORT, restored);
    require(
        glIsEnabled(GL_SCISSOR_TEST) == GL_FALSE && restored[0] == 0 && restored[1] == 0 &&
            restored[2] == extent && restored[3] == extent,
        "Canvas pass did not restore UI viewport/scissor state."
    );
    require(
        cameraPixel(frame, {0, 0.1f})[2] > 200, "Junction point did not follow the shared camera."
    );
    require(
        cameraPixel(frame, {0.25f, -0.18f})[2] > 30,
        "Component did not use the shared camera matrix."
    );
    require(
        cameraPixel(frame, {0.5f, 0.1f})[1] > 200, "Wire did not align with the shared camera."
    );
    require(
        cameraPixel(frame, {0.6f, -0.15f})[1] > 200,
        "Pin did not align with the wire/component camera."
    );
    require(cameraPixel(frame, {0.5f, -0.15f})[2] > 150, "Pin lead used a different transform.");
    require(cameraPixel(frame, {0, -0.4f})[0] > 150, "Grid highlight used a different transform.");
    const std::vector<TextRun> labels{
        {"CAMERA", {0, -0.5f}, 0.002f, {1, 1, 0, 1}},
        {"CLIPPED", {-0.5f, 0.2f}, 0.003f, {1, 1, 0, 1}}
    };
    renderer.drawText(labels, TextSpace::World);
    std::vector<unsigned char> image(extent * extent * 4);
    glReadPixels(0, 0, extent, extent, GL_RGBA, GL_UNSIGNED_BYTE, image.data());
    const auto& viewport = frame.framebufferViewport;
    bool worldText = false;
    for (int y = 0; y < extent; ++y)
        for (int x = 0; x < extent; ++x)
        {
            const auto* p = image.data() + (y * extent + x) * 4;
            const bool inside = x >= viewport.x && x < viewport.x + viewport.width &&
                                y >= viewport.y && y < viewport.y + viewport.height;
            if (!inside)
                require(
                    p[0] < 20 && p[1] < 20 && p[2] < 20,
                    "Canvas grid, points, highlights, or world text leaked into UI space."
                );
            worldText |= inside && p[0] > 180 && p[1] > 180 && p[2] < 60;
        }
    require(worldText, "World text did not appear inside the canvas.");
    saveImage("canvas-viewport.ppm");

    const std::vector<TextRun> overlay{{"UI", {10, 25}, 0.4f, {0, 1, 1, 1}}};
    renderer.drawText(overlay, TextSpace::Screen);
    std::vector<unsigned char> before(180 * 90 * 4), after(before.size());
    glReadPixels(0, extent - 90, 180, 90, GL_RGBA, GL_UNSIGNED_BYTE, before.data());
    bool screenText = false;
    for (std::size_t i = 0; i < before.size(); i += 4)
        screenText |= before[i + 1] > 100 && before[i + 2] > 100;
    require(
        screenText,
        "Screen text stayed clipped to the canvas or used framebuffer pixels as logical units."
    );
    saveImage("canvas-with-overlay.ppm");
    camera.setCenter({-0.1f, 0.3f});
    camera.setZoom(0.7f);
    renderer.beginFrame(camera.frame(frame.surface));
    renderer.drawText(overlay, TextSpace::Screen);
    glReadPixels(0, extent - 90, 180, 90, GL_RGBA, GL_UNSIGNED_BYTE, after.data());
    require(before == after, "Screen overlay moved with canvas pan/zoom.");
    camera.setViewport(CanvasViewport{0, 0, 0, 0});
    renderer.beginFrame(camera.frame(frame.surface));
    renderer.drawCanvas(canvas);
    require(
        renderer.getDrawCallCount() == 0 && glIsEnabled(GL_SCISSOR_TEST) == GL_FALSE,
        "Inactive canvas drew content or left UI scissoring enabled."
    );
    GLint activeViewport[4];
    glGetIntegerv(GL_VIEWPORT, activeViewport);
    require(
        activeViewport[0] == 0 && activeViewport[1] == 0 && activeViewport[2] == extent &&
            activeViewport[3] == extent && glGetError() == GL_NO_ERROR,
        "Canvas completion did not restore the full framebuffer viewport."
    );
}

void routedWiresPresentation(Renderer& renderer, const CanvasCameraFrame& camera)
{
    Scene scene;
    scene.addComponent(BuiltinComponentIds::Input, {-12, 0}, {.inputState = true});
    scene.addComponent(BuiltinComponentIds::And, {0, 0});
    const int output = scene.addComponent(BuiltinComponentIds::Output, {12, -6});
    auto initial = EditorActions(scene).apply({AddWire{{{-11, 0}, {-6, 0}, {-6, -6}, {11, -6}}}});
    require(static_cast<bool>(initial), "Routing visual fixture failed.");
    const Scene before(scene);
    auto draw = [&]
    {
        scene.propagate();
        scene.syncVisuals();
        renderer.beginFrame(camera);
        const auto components = buildComponentPresentation(scene.getComponentViewMap());
        const auto junctions = scene.getWireIntersections();
        renderer.drawCanvas({components, scene.getWires(), junctions});
    };
    require(
        static_cast<bool>(EditorActions(scene).apply({MoveComponent{output, {12, 0}}})),
        "Connected visual move failed."
    );
    draw();
    const auto above = pixel({0, 0.15f}), below = pixel({0, -0.15f});
    require(
        (above[1] > 140 && above[1] > above[0] + 50) ||
            (below[1] > 140 && below[1] > below[0] + 50),
        "Routed powered wire did not render one grid cell above/below the obstacle."
    );
    saveImage("wire-rerouting.ppm");
    require(
        static_cast<bool>(EditorActions(scene).restore(before, scene.getRevision())),
        "Visual restore failed."
    );
    require(
        static_cast<bool>(EditorActions(scene).apply({MoveComponent{output, {12, 0}, false}})),
        "Fixed-wire visual move failed."
    );
    draw();
    require(
        scene.netOfPin({output, 0}, PinType::INPUT) == INVALID_NET_ID,
        "Fixed-wire visual move retained its old attachment."
    );
    saveImage("wire-alt-disconnect.ppm");
}

void circuitViewsPresentation(Renderer& renderer)
{
    CircuitViews views;
    Input input;
    views.select(0, input);
    views.activeScene().addComponent(BuiltinComponentIds::And, {0, 0});
    UI ui;
    ui.setCircuitViews(&views, &renderer.fontMetrics());
    CanvasSurface surface{extent, extent, extent, extent};
    auto layout = [&] { ui.layout(views.activeScene().getComponentCatalog(), surface, input); };
    auto frame = [&]
    {
        CanvasCamera camera;
        camera.setViewport(input.getCanvasViewport());
        camera.setCenter(input.getPanOffset());
        camera.setZoom(input.getZoom());
        return camera.frame(surface);
    };
    auto draw = [&]
    {
        layout();
        const auto camera = frame();
        renderer.beginFrame(camera);
        const auto components =
            buildComponentPresentation(views.activeScene().getComponentViewMap());
        const auto junctions = views.activeScene().getWireIntersections();
        renderer.drawCanvas({components, views.activeScene().getWires(), junctions});
        ui.draw(renderer, views.activeScene(), camera);
        return camera;
    };
    auto screenPixel = [&](double x, double y)
    {
        std::array<unsigned char, 4> value;
        glReadPixels(
            static_cast<int>(x * extent / surface.windowWidth),
            extent - 1 - static_cast<int>(y * extent / surface.windowHeight),
            1,
            1,
            GL_RGBA,
            GL_UNSIGNED_BYTE,
            value.data()
        );
        return value;
    };
    auto click = [&](CanvasViewport bounds, int button)
    {
        UiInputEvent event{UiInputKind::MouseButton};
        event.code = button;
        event.action = GLFW_PRESS;
        event.x = bounds.x + bounds.width / 2;
        event.y = bounds.y + bounds.height / 2;
        if (bounds.y == ui.circuitTabs().tabBounds(0).y &&
            bounds != ui.circuitTabs().tabBounds(0) && bounds != ui.circuitTabs().addBounds())
        {
            const auto main = ui.circuitTabs().tabBounds(0);
            const double left = std::max(bounds.x, main.x + main.width);
            const double right =
                std::min(bounds.x + bounds.width, static_cast<double>(surface.windowWidth));
            require(right > left, "Circuit tab click has no visible target.");
            event.x = (left + right) / 2;
        }
        ui.handleInput(event, views.activeScene(), input, frame());
        layout();
        event.action = GLFW_RELEASE;
        ui.handleInput(event, views.activeScene(), input, frame());
    };
    auto unsavedPixels = [&]
    {
        int count = 0;
        for (int y = 3; y < 27; ++y)
            for (int x = surface.windowWidth / 2; x < surface.windowWidth - 8; ++x)
            {
                const auto pixel = screenPixel(x, y);
                if (pixel[0] > 120 && pixel[1] > 90 && pixel[0] > pixel[2] + 50)
                    ++count;
            }
        return count;
    };
    draw();
    require(
        unsavedPixels() > 30, "Unsaved message did not render at the right of the navigation bar."
    );
    saveImage("unsaved-workspace.ppm");
    views.markSaved(0);
    draw();
    require(unsavedPixels() == 0, "Saved workspace retained the unsaved message.");
    views.rename(0, "Main edited");
    draw();
    require(unsavedPixels() > 30, "Editing a saved workspace did not restore the message.");
    views.rename(0, "Main");
    auto mainFrame = draw();
    require(
        mainFrame.viewport.y == 60 && cameraPixel(mainFrame, {0, 0})[2] > 150,
        "Main tab canvas is not clipped below the header or lost its component."
    );
    const auto mainBounds = ui.circuitTabs().tabBounds(0);
    const auto mainColor = screenPixel(mainBounds.x + 4, mainBounds.y + 5);
    require(
        mainColor[2] > mainColor[0] + 50, "Active Main circuit tab did not draw above the canvas."
    );
    const auto bottom = ui.bottomBounds();
    const auto firstBottomTab = ui.bottomTabBounds(UI::BottomTab::Second);
    require(
        bottom.x == mainFrame.viewport.x &&
            bottom.y == mainFrame.viewport.y + mainFrame.viewport.height &&
            screenPixel(firstBottomTab.x + 3, firstBottomTab.y + 3)[2] > 100,
        "Bottom panel was not reserved beneath the canvas or its active tab was not drawn."
    );
    click(ui.bottomTabBounds(UI::BottomTab::Second), GLFW_MOUSE_BUTTON_LEFT);
    draw();
    saveImage("bottom-panel.ppm");
    const auto selectionMode = ui.modeButtonBounds(EditorMode::Selection);
    const auto interactionMode = ui.modeButtonBounds(EditorMode::Interaction);
    require(
        screenPixel(selectionMode.x + 3, selectionMode.y + 3)[2] <
            screenPixel(interactionMode.x + 3, interactionMode.y + 3)[2],
        "Compact mode controls lost their local faded/available backgrounds."
    );
    require(
        selectionMode.width <= 60 && interactionMode.width <= 60 &&
            screenPixel(selectionMode.x + 3, selectionMode.y + selectionMode.height - 1)[2] >
                screenPixel(
                    interactionMode.x + 3, interactionMode.y + interactionMode.height - 1
                )[2],
        "Compact mode controls lost their active underline."
    );
    click(interactionMode, GLFW_MOUSE_BUTTON_LEFT);
    draw();
    require(
        input.getMode() == EditorMode::Interaction && ui.hoveredMode() == EditorMode::Interaction,
        "Interaction mode button did not update its state or show its popup."
    );
    saveImage("interaction-mode-button.ppm");
    click(selectionMode, GLFW_MOUSE_BUTTON_LEFT);
    draw();
    require(
        input.getMode() == EditorMode::Selection && ui.hoveredMode() == EditorMode::Selection,
        "Selection mode button did not update its state or show its popup."
    );
    saveImage("selection-mode-button.ppm");

    click(ui.fileBounds(), GLFW_MOUSE_BUTTON_LEFT);
    const auto menu = ui.fileMenuBounds();
    const auto saveAs = ui.fileOptionBounds(1);
    ui.handleInput(
        {UiInputKind::Cursor, 0, 0, 0, 0, saveAs.x + 8, saveAs.y + 8},
        views.activeScene(),
        input,
        frame()
    );
    draw();
    require(
        ui.fileMenuOpen() && screenPixel(menu.x + 3, menu.y + 3)[2] > 30 &&
            screenPixel(saveAs.x + 3, saveAs.y + 3)[2] > 100,
        "File dropdown or hovered menu row did not render above the palette."
    );
    saveImage("file-menu.ppm");
    click(ui.fileOptionBounds(0), GLFW_MOUSE_BUTTON_LEFT);
    ui.takeFileCommand();
    require(
        !ui.fileMenuOpen() && views.activeScene().getComponentCount() == 1,
        "Visual Save option changed the scene."
    );
    click(ui.circuitTabs().addBounds(), GLFW_MOUSE_BUTTON_LEFT);
    require(
        ui.circuitTabs().choosingRole() && views.size() == 1,
        "Plus did not open the viewpoint role menu."
    );
    draw();
    saveImage("circuit-view-create.ppm");
    click(ui.circuitTabs().createOptionBounds(1), GLFW_MOUSE_BUTTON_LEFT);
    const auto emptyFrame = draw();
    require(
        ui.subcircuitInfo(views.activeScene())[1].starts_with("Invalid"),
        "Empty subcircuit reported a valid interface."
    );
    saveImage("subcircuit-panel-invalid.ppm");
    require(
        views.activeIndex() == 1 && cameraPixel(emptyFrame, {0, 0})[2] < 100,
        "New circuit still rendered the outgoing scene."
    );
    views.activeScene().addComponent(BuiltinComponentIds::Or, {0, 0});
    auto secondFrame = draw();
    const auto gate = cameraPixel(secondFrame, {0, 0});
    require(
        gate[0] > 150 && gate[2] < 100, "New circuit component was not rendered on its own canvas."
    );
    const auto subTab = ui.circuitTabs().tabBounds(1);
    const auto badgeColor = screenPixel(subTab.x + 11, subTab.y + 20);
    require(
        views.role(1) == CircuitViews::Role::Subcircuit && badgeColor[0] > 65 &&
            badgeColor[2] > 100,
        "Subcircuit authoring tab did not show its badge."
    );
    click(ui.bottomTabBounds(UI::BottomTab::Subcircuit), GLFW_MOUSE_BUTTON_LEFT);
    views.activeScene().addComponent(BuiltinComponentIds::Input, {-10, 0});
    views.activeScene().addComponent(BuiltinComponentIds::Output, {10, 0});
    views.activeScene().addComponent(BuiltinComponentIds::Clock, {20, 0});
    draw();
    require(
        ui.subcircuitInfo(views.activeScene())[1].starts_with("Valid"),
        "Subcircuit panel did not report its named interface as valid."
    );
    saveImage("circuit-view-role.ppm");
    saveImage("subcircuit-panel.ppm");
    views.activeScene().addComponent(BuiltinComponentIds::Input, {-20, 0});
    views.activeScene().addComponent(BuiltinComponentIds::Input, {-30, 0});
    const auto listBounds = ui.subcircuitListBounds();
    ui.handleInput(
        {UiInputKind::Cursor, 0, 0, 0, 0, listBounds.x + 10, listBounds.y + 10},
        views.activeScene(),
        input,
        frame()
    );
    const auto scrollRevision = views.activeScene().getRevision();
    ui.handleInput({UiInputKind::Scroll, 0, 0, 0, 0, 0, -20}, views.activeScene(), input, frame());
    draw();
    require(
        views.activeScene().getRevision() == scrollRevision && input.getZoom() == 1,
        "Subcircuit list scrolling changed the circuit or camera."
    );
    saveImage("subcircuit-panel-scrolled.ppm");
    click(ui.bottomTabBounds(UI::BottomTab::Second), GLFW_MOUSE_BUTTON_LEFT);
    draw();
    const auto add = ui.circuitTabs().addBounds();
    const auto addFill = screenPixel(add.x + 3, add.y + 3);
    require(
        add.width == add.height && addFill[2] > 45 && addFill[0] < 40,
        "Trailing plus square did not draw separately from the circuit tabs."
    );
    saveImage("circuit-tabs.ppm");
    views.rename(1, "A long circuit name that should be shortened in the tab");
    layout();
    require(ui.circuitTabs().tabName(1).ends_with("..."), "Circuit tab title did not shorten.");
    click(ui.circuitTabs().tabBounds(1), GLFW_MOUSE_BUTTON_RIGHT);
    require(ui.circuitTabs().popupIndex() == 1, "Visible circuit tab did not open its name popup.");
    draw();
    const auto popup = ui.circuitTabs().popupBounds();
    const auto field = ui.circuitTabs().nameBounds();
    require(
        screenPixel(popup.x + 2, popup.y + 2)[2] > 25 &&
            screenPixel(field.x + 2, field.y + 2)[2] < 50,
        "Circuit name popup and text field did not render."
    );
    saveImage("circuit-tab-name.ppm");
    surface = {512, 512, extent, extent};
    layout();
    draw();
    const auto dpiBottomTab = ui.bottomTabBounds(UI::BottomTab::Second);
    require(
        screenPixel(dpiBottomTab.x + 3, dpiBottomTab.y + 3)[2] > 100,
        "Bottom panel tab did not follow 2x display scaling."
    );
    saveImage("bottom-panel-2x.ppm");
    click(ui.modeButtonBounds(EditorMode::Interaction), GLFW_MOUSE_BUTTON_LEFT);
    draw();
    const auto tooltip = ui.modeTooltipBounds();
    require(
        tooltip.width > 0 && screenPixel(tooltip.x + 3, tooltip.y + 3)[2] > 30,
        "Mode tooltip did not render at 2x display scaling."
    );
    saveImage("mode-buttons-2x.ppm");
    click(ui.modeButtonBounds(EditorMode::Selection), GLFW_MOUSE_BUTTON_LEFT);
    draw();

    click(ui.bottomTabBounds(UI::BottomTab::Subcircuit), GLFW_MOUSE_BUTTON_LEFT);
    draw();
    require(unsavedPixels() > 30, "Unsaved subcircuit message did not follow 2x display scaling.");
    saveImage("subcircuit-panel-2x.ppm");
    click(ui.fileBounds(), GLFW_MOUSE_BUTTON_LEFT);
    draw();
    const auto dpiMenu = ui.fileMenuBounds();
    require(
        screenPixel(dpiMenu.x + 3, dpiMenu.y + 3)[2] > 30,
        "File dropdown ignored 2x framebuffer scaling."
    );
    saveImage("file-menu-2x.ppm");
    click(ui.fileOptionBounds(0), GLFW_MOUSE_BUTTON_LEFT);
    ui.takeFileCommand();
    click(ui.circuitTabs().tabBounds(1), GLFW_MOUSE_BUTTON_RIGHT);
    require(
        ui.circuitTabs().popupIndex() == 1, "Partially clipped tab could not open its name popup."
    );
    draw();
    const auto dpiPopup = ui.circuitTabs().popupBounds();
    require(
        screenPixel(dpiPopup.x + 2, dpiPopup.y + 2)[2] > 25,
        "Circuit name popup did not scale to 2x DPI."
    );
    saveImage("circuit-tabs-2x.ppm");
    click(ui.circuitTabs().closeBounds(1), GLFW_MOUSE_BUTTON_LEFT);
    require(
        ui.circuitTabs().confirmingDelete() && views.size() == 2,
        "Circuit close button bypassed deletion confirmation."
    );
    draw();
    const auto deletion = ui.circuitTabs().deleteBounds();
    const auto deleteColor = screenPixel(deletion.x + 3, deletion.y + 3);
    require(
        deleteColor[0] > 100 && deleteColor[1] < 60,
        "Circuit deletion confirmation did not render its Delete button."
    );
    saveImage("circuit-tab-delete.ppm");
    click(ui.circuitTabs().cancelDeleteBounds(), GLFW_MOUSE_BUTTON_LEFT);
    require(
        views.size() == 2 && !ui.circuitTabs().popupIndex(), "Cancel did not retain the circuit."
    );
    click(ui.circuitTabs().closeBounds(1), GLFW_MOUSE_BUTTON_LEFT);
    click(ui.circuitTabs().deleteBounds(), GLFW_MOUSE_BUTTON_LEFT);
    require(
        views.size() == 1 && ui.circuitTabs().closeBounds(0).width == 0,
        "Confirmed circuit removal did not retain the permanent Main tab."
    );
    const auto returnedFrame = draw();
    require(
        views.activeIndex() == 0 && cameraPixel(returnedFrame, {0, 0})[2] > 150,
        "Returning to Main did not redraw its retained scene."
    );
    require(
        glGetError() == GL_NO_ERROR && glIsEnabled(GL_SCISSOR_TEST) == GL_FALSE,
        "Circuit tab drawing left a clipping or OpenGL error."
    );
    surface = {extent, extent, extent, extent};
    Scene authored;
    authored.addComponent(BuiltinComponentIds::Input, {-12, 0});
    authored.addComponent(BuiltinComponentIds::Output, {12, 0});
    require(
        static_cast<bool>(EditorActions(authored).apply({AddWire{{{-11, 0}, {11, 0}}}})),
        "Subcircuit rendering fixture failed."
    );
    authored.setInterfaceNamingRequired(true);
    const auto custom = makeSubcircuit(authored, {"subcircuit.render", 1}, "Pass-through");
    views.publishSubcircuits({custom});
    views.mainScene().addComponent(custom.identity.id, {-8, 0});
    views.mainScene().propagate();
    views.mainScene().syncVisuals();
    layout();
    click(ui.tabBounds(UI::Tab::Custom), GLFW_MOUSE_BUTTON_LEFT);
    const auto customFrame = draw();
    require(
        ui.buttons().size() == 1 && ui.buttons()[0].label == "Pass-through",
        "Saved subcircuit was not listed in Custom."
    );
    saveImage("subcircuit-custom.ppm");
    const auto center = *customFrame.worldToWindow({-.4f, 0});
    click({center.x - 1, center.y - 1, 2, 2}, GLFW_MOUSE_BUTTON_RIGHT);
    draw();
    const auto edit = ui.editSubcircuitBounds(views.mainScene());
    require(
        edit.width > 0 && screenPixel(edit.x + 3, edit.y + 3)[2] > 35,
        "Subcircuit information popup did not render its Edit button."
    );
    saveImage("subcircuit-edit.ppm");
    click(edit, GLFW_MOUSE_BUTTON_LEFT);
    require(
        ui.takeEditSubcircuit() == custom.identity.id, "Rendered Edit button did not dispatch."
    );
    views.editSubcircuit(custom, input);
    views.activeScene().propagate();
    views.activeScene().syncVisuals();
    draw();
    click(ui.fileBounds(), GLFW_MOUSE_BUTTON_LEFT);
    draw();
    require(
        ui.fileOptions().size() == 2 && ui.fileOptionBounds(2).width == 0,
        "Subcircuit File menu retained Main's commands."
    );
    saveImage("subcircuit-file-menu.ppm");
    for (int scale : {1, 2})
    {
        surface = {extent / scale, extent / scale, extent, extent};
        draw();
        const auto behind = screenPixel(surface.windowWidth - 10, 24);
        // Diagnostics must ignore any remaining UI clipping and blend over the UI itself.
        renderer.setScreenClip(CanvasViewport{0, 0, 10, 10});
        renderer.drawDebugOverlay(DebugMetrics{});
        const auto shaded = screenPixel(surface.windowWidth - 10, 24);
        for (int channel = 0; channel < 3; ++channel)
            require(
                std::abs(2 * static_cast<int>(shaded[channel]) - behind[channel]) <= 2,
                "Debug background did not blend at 50% over the UI at this display scale."
            );
        require(glIsEnabled(GL_SCISSOR_TEST) == GL_FALSE, "Debug overlay retained UI clipping.");
        saveImage(scale == 1 ? "debug-overlay-ui.ppm" : "debug-overlay-ui-2x.ppm");
        draw();
        renderer.drawDebugOverlay(DebugMetrics{}, false);
        require(
            screenPixel(surface.windowWidth - 10, 24) == behind,
            "Hidden F3 diagnostics left a background visible."
        );
    }
    ui.dismissPopups(input);
    ui.setCircuitViews(nullptr, nullptr);
    input.setScene(nullptr);
}

} // namespace

int main()
{
    if (!glfwInit())
        return 77;
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    auto* window = glfwCreateWindow(64, 64, "Rendering regression", nullptr, nullptr);
    if (!window)
    {
        glfwTerminate();
        return 77;
    }
    glfwMakeContextCurrent(window);
    if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress)))
    {
        glfwDestroyWindow(window);
        glfwTerminate();
        return 77;
    }
    GLuint texture = 0, framebuffer = 0;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, extent, extent, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glGenFramebuffers(1, &framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
    glViewport(0, 0, extent, extent);
    int result = 0;
    {
        Renderer renderer;
        try
        {
            require(
                glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE,
                "Framebuffer incomplete."
            );
            require(renderer.init(), "Renderer could not initialize required resources.");
            const auto camera = CanvasCamera{}.frame({extent, extent, extent, extent});
            mixedInstances(renderer, camera);
            nativeContacts(renderer, camera);
            editedGate(renderer, camera);
            preview(renderer, camera);
            canvasViewport(renderer);
            wireMarkers(renderer, camera);
            roundedBounds(renderer, camera);
            componentOutlines(renderer);
            palettePresentation(renderer);
            outputPresentation(renderer);
            componentInformationPresentation(renderer);
            routedWiresPresentation(renderer, camera);
            circuitViewsPresentation(renderer);
            std::cout << "PASS: render_pixels (OpenGL " << glGetString(GL_VERSION) << ")\n";
        }
        catch (const std::exception& error)
        {
            std::cerr << "FAIL: " << error.what() << '\n';
            result = 1;
        }
        renderer.shutdown();
        renderer.shutdown();
    }
    glDeleteFramebuffers(1, &framebuffer);
    glDeleteTextures(1, &texture);
    glfwDestroyWindow(window);
    glfwTerminate();
    return result;
}
