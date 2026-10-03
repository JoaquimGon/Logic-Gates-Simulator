#include "Components/Definitions/NativeDefinitions.h"
#include "Editor/Actions/EditorActions.h"
#include "Editor/Input.h"
#include "Editor/Scene.h"
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
                if (std::abs(value[0] - tint[0] * 255) < 8 &&
                    std::abs(value[1] - tint[1] * 255) < 8 &&
                    std::abs(value[2] - tint[2] * 255) < 8)
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
    ui.handleInput({UiInputKind::Cursor, 0, 0, 0, 0, 30, 120}, scene, input, dpi);
    ui.handleInput({UiInputKind::Scroll, 0, 0, 0, 0, 0, -20}, scene, input, dpi);
    const auto partial = std::find_if(
        ui.buttons().begin(),
        ui.buttons().end(),
        [](const auto& item)
        { return item.bounds.y < 112 && item.bounds.y + item.bounds.height > 112; }
    );
    require(partial != ui.buttons().end(), "Framebuffer scroll fixture has no partial card.");
    renderer.beginFrame(dpi);
    renderer.drawCanvas({components, scene.getWires(), junctions});
    ui.draw(renderer, scene, dpi);
    const double edgeX = partial->bounds.x + 3;
    const auto cardEdge = screenPixel({edgeX, 115}, 2);
    const auto panelBackground = screenPixel({5, 115}, 2);
    require(
        cardEdge[2] > panelBackground[2] && screenPixel({edgeX, 110}, 2) == panelBackground &&
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
            roundedBounds(renderer, camera);
            palettePresentation(renderer);
            outputPresentation(renderer);
            componentInformationPresentation(renderer);
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
