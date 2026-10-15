#include "Components/Definitions/NativeDefinitions.h"
#include "Editor/Actions/EditorActions.h"
#include "Editor/CircuitViews.h"
#include "Editor/Input.h"
#include "Editor/Scene.h"
#include "Editor/Subcircuits.h"
#include "Graphics/Presentation/ComponentPresentation.h"
#include "Graphics/Presentation/LabelLayout.h"
#include "Graphics/Renderer.h"
#include "UI/UI.h"

#include <GLFW/glfw3.h>
#include <algorithm>
#include <array>
#include <chrono>
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

void drawingProfile(Renderer&, const CanvasCameraFrame&);
void renderResourceChecks();
void debugOverlayCacheChecks(Renderer&);

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
    const auto byId = [](const auto& a, const auto& b) { return a.body.id < b.body.id; };
    std::sort(data.begin(), data.end(), byId);
    // The last red batch matches the first batch of the next frame: its upload is
    // cached, but the GPU must still fetch red after the intervening green batch.
    for (int repeat = 0; repeat < 2; ++repeat)
        do
        {
            renderer.beginFrame(camera);
            renderer.drawComponents(data);
            require(
                renderer.getDrawCallCount() == 3 && pixel({0, 0}) == expected,
                "Container order or cached instance data changed rendered pixels."
            );
        } while (std::next_permutation(data.begin(), data.end(), byId));
    renderer.beginFrame(camera);
    renderer.drawComponents({});
    require(renderer.getDrawCallCount() == 0, "Empty bodies counted a draw.");
}

void performanceOverlay(Renderer& renderer)
{
    CircuitViews views;
    Input input;
    views.select(0, input);
    for (int i = 0; i < 3; ++i)
        views.activeScene().addComponent(BuiltinComponentIds::Clock, {i * 6, 0});
    views.activeScene().stepAllClocks(1, &views.activeClockTimeline());
    views.activeScene().stepAllClocks(2, &views.activeClockTimeline());
    views.activeScene().addComponent(BuiltinComponentIds::And, {-6, 0});
    views.activeScene().propagate();
    auto components = buildComponentPresentation(views.activeScene().getComponentViewMap());
    auto custom = components.back();
    custom.body.id = 1000;
    custom.body.shader = "box";
    custom.body.position = {0.5f, 0.5f};
    custom.bodyLabel = "Custom box";
    custom.pins.clear();
    components.push_back(custom);
    Wire wire;
    wire.setPath({{-8, 0}, {-4, 0}});
    const std::map<WireId, Wire> wires{{1, wire}};
    for (int dpi : {1, 2})
    {
        UI ui;
        ui.setCircuitViews(&views, &renderer.fontMetrics());
        const CanvasSurface surface{extent / dpi, extent / dpi, extent, extent};
        ui.layout(views.activeScene().getComponentCatalog(), surface, input);
        CanvasCamera camera;
        camera.setViewport(input.getCanvasViewport());
        const auto frame = camera.frame(surface);
        auto select = [&](UI::BottomTab which)
        {
            const auto tab = ui.bottomTabBounds(which);
            UiInputEvent event{UiInputKind::MouseButton};
            event.code = GLFW_MOUSE_BUTTON_LEFT;
            event.x = tab.x + tab.width / 2;
            event.y = tab.y + tab.height / 2;
            event.action = GLFW_PRESS;
            ui.handleInput(event, views.activeScene(), input, frame);
            event.action = GLFW_RELEASE;
            ui.handleInput(event, views.activeScene(), input, frame);
        };
        select(UI::BottomTab::Clocks);
        renderer.beginFrame(frame);
        renderer.drawCanvas({components, wires, {}});
        const auto canvasCounts = renderer.getDrawCallCounts();
        require(
            canvasCounts[DrawCategory::StaticUI] == 1 &&
                canvasCounts[DrawCategory::Components] >= 3 &&
                canvasCounts[DrawCategory::Wires] == 1 &&
                canvasCounts[DrawCategory::DynamicUI] == 0,
            "Canvas batches, grid or wires were counted in the wrong category."
        );
        renderer.beginUiTiming(true);
        ui.draw(renderer, views.activeScene(), frame);
        const auto cpu = renderer.endUiTiming();
        require(
            cpu[CpuPhase::StaticUI] > 0 && cpu[CpuPhase::DynamicUI] > 0,
            "CPU timings did not capture static and clock-panel construction/submission."
        );
        const auto counts = renderer.getDrawCallCounts();
        require(
            counts[DrawCategory::StaticUI] > 20 && counts[DrawCategory::DynamicUI] > 8 &&
                counts[DrawCategory::Components] == canvasCounts[DrawCategory::Components] &&
                counts[DrawCategory::Wires] == canvasCounts[DrawCategory::Wires] &&
                renderer.getDrawCallCount() == counts.total(),
            "UI draws were missing or palette previews contaminated canvas component counts."
        );
        DebugMetrics metrics;
        metrics.fps = 60;
        metrics.frameTimeMs = 1000.0f / 60;
        metrics.drawCalls = counts;
        metrics.averageCpu = metrics.worstCpu = cpu;
        metrics.timingsReady = true;
        metrics.totalComponents = views.activeScene().getComponentCount();
        metrics.simulationWork = views.activeScene().takeSimulationWork();
        metrics.averageCpu[CpuPhase::Propagation] = metrics.simulationWork.propagationMs;
        metrics.worstCpu[CpuPhase::Propagation] = metrics.simulationWork.propagationMs;
        renderer.drawDebugOverlay(metrics);
        require(
            renderer.getDrawCallCounts() == counts && glGetError() == GL_NO_ERROR &&
                glIsEnabled(GL_SCISSOR_TEST) == GL_FALSE,
            "Debug overlay counted itself or left invalid OpenGL/clipping state."
        );
        const auto runs = layoutDebugOverlay(
            metrics, true, surface.windowWidth, surface.windowHeight, renderer.fontMetrics()
        );
        auto row = std::find_if(
            runs.begin(),
            runs.end(),
            [](const auto& run) { return run.text.starts_with("Dynamic UI"); }
        );
        require(row != runs.end(), "Dynamic UI timing row was not displayed.");
        int ink = 0;
        for (int y = static_cast<int>(row->baseline.y - 14); y < row->baseline.y; ++y)
            for (int x = static_cast<int>(row->baseline.x); x < surface.windowWidth - 16; ++x)
            {
                std::array<unsigned char, 4> color;
                glReadPixels(
                    x * dpi, extent - 1 - y * dpi, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, color.data()
                );
                if (color[0] > 150 && color[1] > 110)
                    ++ink;
            }
        require(ink > 30, "CPU table text did not render legibly at this DPI.");
        saveImage(dpi == 1 ? "debug-performance.ppm" : "debug-performance-2x.ppm");
        select(UI::BottomTab::Messages);
        renderer.invalidateDebugOverlay();
        renderer.beginFrame(frame);
        require(renderer.getDrawCallCount() == 0, "Frame counters did not reset.");
        renderer.drawCanvas({components, wires, {}});
        renderer.beginUiTiming(true);
        ui.draw(renderer, views.activeScene(), frame);
        const auto messagesCpu = renderer.endUiTiming();
        require(
            messagesCpu[CpuPhase::DynamicUI] == 0 && messagesCpu[CpuPhase::StaticUI] > 0,
            "Hidden clock panel continued contributing dynamic UI CPU time."
        );
        require(
            renderer.getDrawCallCounts()[DrawCategory::DynamicUI] == 0,
            "Hidden clock panel continued contributing dynamic UI draw calls."
        );
        metrics.drawCalls = renderer.getDrawCallCounts();
        metrics.averageCpu = metrics.worstCpu = messagesCpu;
        renderer.drawDebugOverlay(metrics);
        saveImage(
            dpi == 1 ? "debug-performance-messages.ppm" : "debug-performance-messages-2x.ppm"
        );
        const auto point = *frame.worldToWindow({0, 0});
        UiInputEvent popup{UiInputKind::MouseButton};
        popup.code = GLFW_MOUSE_BUTTON_RIGHT;
        popup.action = GLFW_PRESS;
        popup.x = point.x;
        popup.y = point.y;
        ui.handleInput(popup, views.activeScene(), input, frame);
        popup.action = GLFW_RELEASE;
        ui.handleInput(popup, views.activeScene(), input, frame);
        require(ui.infoComponentId() != -1, "Live information profiling fixture did not open.");
        renderer.beginUiTiming(true);
        ui.draw(renderer, views.activeScene(), frame);
        const auto infoCpu = renderer.endUiTiming();
        require(
            infoCpu[CpuPhase::DynamicUI] > 0,
            "Live component information was charged to static UI CPU time."
        );
        ui.dismissPopups(input);
        renderer.beginUiTiming(false);
        ui.draw(renderer, views.activeScene(), frame);
        const auto disabledCpu = renderer.endUiTiming();
        require(
            std::all_of(
                disabledCpu.ms.begin(), disabledCpu.ms.end(), [](double ms) { return ms == 0; }
            ),
            "Hidden F3 still collected UI CPU timings."
        );
    }
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
                if (body.inputRail && pin.direction == PinType::INPUT &&
                    std::abs(pin.position.y) > 0.38f * body.body.size.y)
                    continue; // Outer rows contact the extension, tested separately below.
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
        if (inverted.inputRail && pin.direction == PinType::INPUT &&
            std::abs(pin.position.y) > 0.38f * inverted.body.size.y)
            continue;
        const auto contact = pixel(pin.lead.front() * 0.99f);
        require(contact[2] > 25, "Edited inversion lead misses the rendered body.");
    }
    saveImage("edited-gate-inverted.ppm");
}

void inputRailPresentation(Renderer& renderer, const CanvasCameraFrame& camera)
{
    auto bodyPixels = [&]
    {
        std::vector<unsigned char> image(256 * 256 * 4);
        glReadPixels(
            extent / 2 - 128, extent / 2 - 128, 256, 256, GL_RGBA, GL_UNSIGNED_BYTE, image.data()
        );
        return image;
    };
    for (const auto type : {AND, NAND, OR, NOR, XOR, NXOR})
    {
        Scene scene;
        const int id = scene.addComponent(builtinDefinitionId(type), {0, 0});
        EditorActions actions(scene);
        auto original = buildComponentPresentation(scene.getComponentViewMap());
        original.front().body.drawRearArc = false;
        renderer.beginFrame(camera);
        renderer.drawComponents(original);
        const auto before = bodyPixels();
        require(
            static_cast<bool>(
                actions.apply({ConfigureComponentProperties{.componentId = id, .inputCount = 8}})
            ),
            "Cannot create input rail rendering fixture."
        );
        const auto expanded = buildComponentPresentation(scene.getComponentViewMap());
        renderer.beginFrame(camera);
        renderer.drawComponents(expanded);
        require(bodyPixels() == before, "Changing input count stretched/deformed the gate body.");
        renderer.beginFrame(camera);
        renderer.drawPinLeads(expanded);
        renderer.drawComponents(expanded);
        renderer.drawPins(expanded);
        const auto point =
            expanded.front().inputArc.empty()
                ? glm::vec2{expanded.front().inputRail->centerX(), 0.175f}
                : (expanded.front().inputArc[59] + expanded.front().inputArc[60]) * 0.5f;
        if (!expanded.front().inputArc.empty())
        {
            const auto center = expanded.front().inputArc[32];
            const float pixelWidth = 1.0f / camera.pixelsPerWorldUnit;
            int smoothPixels = 0;
            for (int offset = -5; offset <= 5; ++offset)
            {
                const auto edge = pixel(center + glm::vec2{offset * pixelWidth, 0});
                smoothPixels += edge[0] > 40 && edge[0] < 160 && edge[2] > 65 && edge[2] < 210;
            }
            require(
                smoothPixels > 0, "Extended XOR arc has hard pixel edges without antialiasing."
            );
        }
        const auto rail = pixel(point);
        const auto tint = expanded.front().body.style.tint;
        require(
            std::abs(rail[0] - tint[0] * 255) < 2 && std::abs(rail[2] - tint[2] * 255) < 2,
            "Input rail was missing or used electrical wire coloring."
        );
        const auto name = std::string("input-rail-") +
                          (type == AND    ? "and"
                           : type == NAND ? "nand"
                           : type == OR   ? "or"
                           : type == NOR  ? "nor"
                           : type == XOR  ? "xor"
                                          : "nxor") +
                          ".ppm";
        saveImage(name.c_str());
        require(
            static_cast<bool>(
                actions.apply({ConfigureComponentProperties{.componentId = id, .inputCount = 2}})
            ),
            "Cannot shrink rail fixture."
        );
        const auto shrunk = buildComponentPresentation(scene.getComponentViewMap());
        renderer.beginFrame(camera);
        renderer.drawPinLeads(shrunk);
        renderer.drawComponents(shrunk);
        renderer.drawPins(shrunk);
        require(pixel(point)[2] < 25, "Removed input rail remained in the reused lead buffer.");
    }
}

void inversionProportions(Renderer& renderer, const CanvasCameraFrame& camera)
{
    auto image = [&]
    {
        std::vector<unsigned char> pixels(256 * 256 * 4);
        glReadPixels(
            extent / 2 - 128, extent / 2 - 128, 256, 256, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data()
        );
        return pixels;
    };
    for (auto type : {AND, NAND, OR, NOR, XOR, NXOR})
    {
        Scene scene;
        const int id = scene.addComponent(builtinDefinitionId(type), {0, 0});
        for (int pass = 0; pass < 2; ++pass)
        {
            const auto* gate = static_cast<const Gate*>(scene.getLogicComponent(id));
            const bool inverted = !gate->isInverted();
            const auto target = Gate::typeWithInversion(gate->getType(), inverted);
            require(
                static_cast<bool>(EditorActions(scene).apply(
                    {ConfigureComponentProperties{.componentId = id, .inverted = inverted}}
                )),
                "Cannot switch native gate inversion."
            );
            const auto data = buildComponentPresentation(scene.getComponentViewMap());
            renderer.beginFrame(camera);
            renderer.drawComponents(data);
            const auto changed = image();
            Scene canonical;
            const int reference = canonical.addComponent(builtinDefinitionId(target), {0, 0});
            renderer.beginFrame(camera);
            renderer.drawComponents(buildComponentPresentation(canonical.getComponentViewMap()));
            require(
                changed == image(),
                "Inversion warped the body/bubble compared with spawning the target gate."
            );
            const auto* actual = scene.getCommittedComponentView(id);
            const auto* expected = canonical.getCommittedComponentView(reference);
            require(
                actual->getOutputPins()[0].relative_pos ==
                    expected->getOutputPins()[0].relative_pos,
                "Inversion left the output pin at the wrong side of the bubble."
            );
            renderer.beginFrame(camera);
            renderer.drawPinLeads(data);
            renderer.drawComponents(data);
            renderer.drawPins(data);
            const auto name = std::string("gate-switch-") + builtinDefinitionId(type) +
                              (pass ? "-back.ppm" : "-paired.ppm");
            saveImage(name.c_str());
        }
    }
}

void inputInversionPresentation(Renderer& renderer, const CanvasCameraFrame& camera)
{
    Scene scene;
    int row = 0;
    for (const auto type : {NOT, AND, NAND, OR, NOR, XOR, NXOR})
    {
        const int id = scene.addComponent(
            builtinDefinitionId(type),
            {-12 + (row % 4) * 8, 9 - (row / 4) * 16},
            {.inputCount = type == NOT ? 1 : 8}
        );
        const auto result = EditorActions(scene).apply({ConfigureComponentProperties{
            .componentId = id, .inputInversion = ConfigureGateInput{0, true}
        }});
        require(static_cast<bool>(result), "Cannot prepare input-bubble render fixture.");
        ++row;
    }
    const auto data = buildComponentPresentation(scene.getComponentViewMap());
    renderer.beginFrame(camera);
    renderer.drawPinLeads(data);
    renderer.drawComponents(data);
    renderer.drawPins(data);
    for (const auto& component : data)
        for (const auto& pin : component.pins)
            if (pin.inversionBubble)
            {
                const auto center = pixel(*pin.inversionBubble);
                const auto rim = pixel(*pin.inversionBubble + glm::vec2{0, 0.023f});
                const auto background = pixel(*pin.inversionBubble + glm::vec2{0, 0.035f});
                require(
                    center[1] > 60 && (center[0] > 60 || center[2] > 60),
                    "Input bubble did not retain its gate fill."
                );
                require(
                    rim[0] < center[0] || rim[2] < center[2],
                    "Input bubble lost its visible outline."
                );
                require(
                    background[0] < 20 && background[1] < 20 && background[2] < 20,
                    "Input bubble was stretched beyond its one-cell diameter."
                );
            }
    require(glGetError() == GL_NO_ERROR, "Input bubbles caused an OpenGL error.");
    saveImage("gate-input-inversion.ppm");
}

void orientationPresentation(Renderer& renderer, const CanvasCameraFrame& camera)
{
    for (const auto direction :
         {Orientation::Right, Orientation::Up, Orientation::Left, Orientation::Down})
    {
        Scene scene;
        int column = 0;
        for (const auto type : {NOT, NAND, XOR})
        {
            const int id = scene.addComponent(
                builtinDefinitionId(type),
                {-10 + column++ * 10, 0},
                {.inputCount = type == NOT ? 1 : 8}
            );
            require(
                static_cast<bool>(EditorActions(scene).apply({ConfigureComponentProperties{
                    .componentId = id,
                    .inputInversion = ConfigureGateInput{0, true},
                    .orientation = direction
                }})),
                "Cannot prepare rotated render fixture."
            );
        }
        const auto data = buildComponentPresentation(scene.getComponentViewMap());
        renderer.beginFrame(camera);
        renderer.drawPinLeads(data);
        renderer.drawComponents(data);
        renderer.drawPins(data);
        for (const auto& component : data)
        {
            const auto offset = rotatePoint(
                component.body.style.contour == BodyContour::Not ? -0.3f * component.body.size.x
                                                                 : 0,
                0,
                direction
            );
            const auto fill = pixel(component.body.position + glm::vec2{offset[0], offset[1]});
            require(
                fill[1] > 50 && fill[2] > 120,
                "Rotated shader body did not match its expected position."
            );
            for (const auto& pin : component.pins)
            {
                const auto dot = pixel(pin.position);
                require(
                    dot[0] > 200 || dot[1] > 200 || dot[2] > 200,
                    "Rotated pin was not drawn at its electrical anchor."
                );
                if (pin.inversionBubble)
                {
                    const auto bubble = pixel(*pin.inversionBubble);
                    require(
                        bubble[1] > 50 && bubble[2] > 120,
                        "Input bubble did not follow the rotated gate."
                    );
                }
            }
            if (!component.inputArc.empty())
            {
                const auto arc = pixel(component.inputArc[8]);
                require(
                    arc[0] > 50 || arc[2] > 80,
                    "Extended XOR arc stayed in its previous orientation."
                );
            }
            else if (component.inputRail)
            {
                const auto rail = *component.inputRail;
                const auto rotated =
                    rotatePoint(rail.centerX() - component.body.position.x, 0.175f, direction);
                const auto extension =
                    pixel(component.body.position + glm::vec2{rotated[0], rotated[1]});
                require(extension[2] > 70, "Expanded input rail did not rotate with the body.");
            }
        }
        const auto name = std::string("gate-orientation-") + orientationName(direction) + ".ppm";
        saveImage(name.c_str());
    }
    require(glGetError() == GL_NO_ERROR, "Gate orientation caused an OpenGL error.");
}

void generalizedOrientationPresentation(Renderer& renderer, const CanvasCameraFrame& camera)
{
    Scene authored;
    authored.setInterfaceNamingRequired(true);
    authored.addComponent(BuiltinComponentIds::Input, {-10, 0});
    authored.addComponent(BuiltinComponentIds::Output, {10, 0});
    require(
        static_cast<bool>(EditorActions(authored).apply({AddWire{{{-9, 0}, {9, 0}}}})),
        "Cannot prepare subcircuit rendering fixture."
    );
    const auto custom = makeSubcircuit(authored, {"subcircuit.render", 1}, "Pass through");
    for (const auto direction :
         {Orientation::Right, Orientation::Up, Orientation::Left, Orientation::Down})
    {
        Scene scene;
        require(
            static_cast<bool>(EditorActions(scene).apply(
                {RegisterComponentDefinition{custom},
                 CreateComponent{
                     .definitionId = BuiltinComponentIds::SrLatch,
                     .position = {-8, 0},
                     .orientation = direction
                 },
                 CreateComponent{
                     .definitionId = custom.identity.id,
                     .position = {8, 0},
                     .orientation = direction
                 }}
            )),
            "Cannot create rotated latch/subcircuit rendering fixture."
        );
        const auto data = buildComponentPresentation(scene.getComponentViewMap());
        renderer.beginFrame(camera);
        renderer.drawPinLeads(data);
        renderer.drawComponents(data);
        renderer.drawPins(data);
        for (const auto& component : data)
        {
            const auto rotated = rotatePoint(0.4f * component.body.size.x, 0, direction);
            const auto fill = pixel(component.body.position + glm::vec2{rotated[0], rotated[1]});
            require(fill[2] > 30, "Latch/subcircuit box lost its rotated dimensions.");
            for (const auto& pin : component.pins)
            {
                const auto dot = pixel(pin.position);
                require(
                    dot[0] > 200 || dot[1] > 200 || dot[2] > 200,
                    "Latch/subcircuit pin was not drawn at its rotated anchor."
                );
            }
        }
        const auto labels = layoutComponentLabels(data, renderer.fontMetrics());
        require(labels.size() == 8, "Rotation omitted a latch/subcircuit body or pin label.");
        renderer.drawLabels(data);
        const auto name =
            std::string("latch-subcircuit-orientation-") + orientationName(direction) + ".ppm";
        saveImage(name.c_str());
        if (direction == Orientation::Up)
        {
            Input input;
            input.setScene(&scene);
            input.setMode(EditorMode::Selection);
            UI ui;
            ui.layout(scene.getComponentCatalog(), camera.surface, input);
            CanvasCamera popupCamera;
            popupCamera.setViewport(input.getCanvasViewport());
            const auto frame = popupCamera.frame(camera.surface);
            for (int id : {0, 1})
            {
                ui.dismissPopups(input);
                const auto point =
                    *frame.worldToWindow(scene.getCommittedComponentView(id)->getPosition());
                for (const auto action : {GLFW_PRESS, GLFW_RELEASE})
                    ui.handleInput(
                        {UiInputKind::MouseButton,
                         GLFW_MOUSE_BUTTON_RIGHT,
                         action,
                         0,
                         0,
                         point.x,
                         point.y},
                        scene,
                        input,
                        frame
                    );
                require(
                    ui.infoComponentId() == id &&
                        ui.componentOrientationBounds(scene, Orientation::Up).width > 0 &&
                        ui.gateSettingsBounds(scene).width == 0,
                    "Latch/subcircuit popup lost orientation or exposed gate-only settings."
                );
                renderer.beginFrame(frame);
                renderer.drawCanvas({data, scene.getWires(), {}});
                ui.draw(renderer, scene, frame);
                saveImage(
                    id == 0 ? "latch-orientation-popup.ppm" : "subcircuit-orientation-popup.ppm"
                );
            }
            input.setScene(nullptr);
        }
    }
    require(glGetError() == GL_NO_ERROR, "Latch/subcircuit orientation caused an OpenGL error.");
}

void pastePreview(Renderer& renderer, const CanvasCameraFrame& camera)
{
    Scene scene;
    const int source = scene.addComponent(BuiltinComponentIds::And, {0, 0});
    Selection selection;
    selection.selectComponent(source);
    EditorClipboard clipboard;
    clipboard.copy(scene, selection);
    PasteGesture paste;
    require(
        static_cast<bool>(paste.begin(scene, clipboard, {8, 0})), "Paste preview creation failed."
    );
    const auto components = buildComponentPresentation(scene.getComponentViewMap());
    auto ghosts = buildComponentPresentation(paste.scene()->getComponentViewMap());
    CanvasFrame frame{components, scene.getWires(), {}};
    renderer.beginFrame(camera);
    renderer.drawCanvas(frame);
    const auto background = pixel({0.4f, 0});
    renderer.drawComponents(ghosts);
    const auto solid = pixel({0.4f, 0});
    frame.pasteComponents = ghosts;
    frame.pasteWires = &paste.scene()->getVisibleWires();
    renderer.beginFrame(camera);
    renderer.drawCanvas(frame);
    const auto ghost = pixel({0.4f, 0});
    require(
        ghost[2] > background[2] + 10 && ghost[2] + 10 < solid[2],
        "Paste preview bodies were invisible or fully opaque."
    );
    saveImage("paste-preview-valid.ppm");
    paste.update(scene, {1, 0});
    ghosts = buildComponentPresentation(paste.scene()->getComponentViewMap());
    frame.pasteComponents = ghosts;
    std::vector<BodyBounds> overlapping;
    for (int id : paste.overlappingComponents())
        overlapping.push_back(paste.scene()->getComponentViewMap().at(id)->getBodyBounds());
    renderer.beginFrame(camera);
    renderer.drawCanvas(frame);
    const auto beforeNotice = pixel({0, 0});
    frame.overlappingPasteBodies = overlapping;
    renderer.beginFrame(camera);
    renderer.drawCanvas(frame);
    require(
        pixel({0, 0})[0] > beforeNotice[0] + 20 && pixel({0, 0})[1] > beforeNotice[1] + 5 &&
            pixel({0, 0})[2] + 20 < beforeNotice[2],
        "Overlapping paste has no visible amber placement notice."
    );
    saveImage("paste-preview-overlap.ppm");
    paste.update(scene, {0, 0});
    ghosts = buildComponentPresentation(paste.scene()->getComponentViewMap());
    frame.pasteComponents = ghosts;
    frame.overlappingPasteBodies = {};
    std::vector<BodyBounds> blocked;
    for (int id : paste.blockedComponents())
        blocked.push_back(paste.scene()->getComponentViewMap().at(id)->getBodyBounds());
    renderer.beginFrame(camera);
    renderer.drawCanvas(frame);
    const auto beforeRed = pixel({0, 0});
    frame.blockedPasteBodies = blocked;
    renderer.beginFrame(camera);
    renderer.drawCanvas(frame);
    require(
        pixel({0, 0})[0] > beforeRed[0] + 35, "Exact-center collision has no visible red highlight."
    );
    saveImage("paste-preview-same-center.ppm");
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

void flipFlopPresentation(Renderer& renderer)
{
    for (int scale : {1, 2})
    {
        Scene scene;
        Input input;
        input.setScene(&scene);
        UI ui;
        const CanvasSurface surface{extent / scale, extent / scale, extent, extent};
        ui.layout(scene.getComponentCatalog(), surface, input);
        CanvasCamera camera;
        camera.setViewport(input.getCanvasViewport());
        const auto frame = camera.frame(surface);
        for (auto [type, x] :
             {std::pair{FlipFlopType::D, -9},
              std::pair{FlipFlopType::T, 0},
              std::pair{FlipFlopType::JK, 9}})
            scene.addComponent(builtinDefinitionId(type), {x, 0});
        scene.propagate();
        scene.syncVisuals();
        const auto components = buildComponentPresentation(scene.getComponentViewMap());
        const auto junctions = scene.getWireIntersections();
        auto draw = [&]
        {
            renderer.beginFrame(frame);
            renderer.drawCanvas({components, scene.getWires(), junctions});
            ui.draw(renderer, scene, frame);
        };
        draw();
        saveImage(scale == 1 ? "flip-flops.ppm" : "flip-flops-2x.ppm");
        const auto point = *frame.worldToWindow(GridSystem::gridToWorld({9, 0}));
        for (const int action : {GLFW_PRESS, GLFW_RELEASE})
            ui.handleInput(
                {UiInputKind::MouseButton, GLFW_MOUSE_BUTTON_RIGHT, action, 0, 0, point.x, point.y},
                scene,
                input,
                frame
            );
        const auto info = ui.componentInfo(scene);
        require(
            ui.infoComponentId() == 2 &&
                std::find(info.begin(), info.end(), "JK: 10 set, 11 toggle") != info.end(),
            "Flip-flop popup omitted its edge/next-state explanation."
        );
        draw();
        saveImage(scale == 1 ? "flip-flop-information.ppm" : "flip-flop-information-2x.ppm");
        input.setScene(nullptr);
    }
}

void truthTablePresentation(Renderer& renderer)
{
    for (int scale : {1, 2})
        for (const auto definition :
             {BuiltinComponentIds::Not,
              BuiltinComponentIds::And,
              BuiltinComponentIds::SrLatch,
              BuiltinComponentIds::DLatch,
              BuiltinComponentIds::JkFlipFlop})
        {
            Scene scene;
            const int id = scene.addComponent(definition, {0, 0});
            if (definition == BuiltinComponentIds::And)
                require(
                    static_cast<bool>(EditorActions(scene).apply({ConfigureComponentProperties{
                        .componentId = id,
                        .inputCount = 8,
                        .inverted = true,
                        .inputInversion = ConfigureGateInput{0, true}
                    }})),
                    "Cannot configure the rendered truth table."
                );
            auto* logic = scene.getLogicComponent(id);
            for (int i = 0; i < logic->getInputPinCount(); ++i)
                logic->setStateInPin(i, i != logic->clockInputPin());
            scene.markSimulationDirty();
            scene.propagate();
            scene.syncVisuals();
            Input input;
            input.setScene(&scene);
            UI ui;
            const CanvasSurface surface{extent / scale, extent / scale, extent, extent};
            ui.layout(scene.getComponentCatalog(), surface, input);
            CanvasCamera camera;
            camera.setViewport(input.getCanvasViewport());
            const auto frame = camera.frame(surface);
            auto click = [&](CanvasViewport bounds, int button)
            {
                for (int action : {GLFW_PRESS, GLFW_RELEASE})
                    ui.handleInput(
                        {UiInputKind::MouseButton,
                         button,
                         action,
                         0,
                         0,
                         bounds.x + bounds.width / 2,
                         bounds.y + bounds.height / 2},
                        scene,
                        input,
                        frame
                    );
            };
            const auto point = *frame.worldToWindow({0, 0});
            click({point.x, point.y, 0, 0}, GLFW_MOUSE_BUTTON_RIGHT);
            click(ui.infoTabBounds(scene, true), GLFW_MOUSE_BUTTON_LEFT);
            require(ui.truthTableOpen(), "Truth-table rendering fixture did not open.");
            auto draw = [&]
            {
                renderer.beginFrame(frame);
                const auto components = buildComponentPresentation(scene.getComponentViewMap());
                renderer.drawCanvas({components, scene.getWires(), {}});
                ui.draw(renderer, scene, frame);
            };
            draw();
            const auto current = ui.truthTableCurrentRow(scene);
            require(current.has_value(), "Rendered table omitted the current input row.");
            const auto area = ui.truthTableRowsBounds(scene);
            const double y = area.y + (*current - ui.truthTableScroll(scene)) * 22 + 10;
            std::array<unsigned char, 4> color{};
            glReadPixels(
                static_cast<int>((area.x + 2) * scale),
                extent - 1 - static_cast<int>(y * scale),
                1,
                1,
                GL_RGBA,
                GL_UNSIGNED_BYTE,
                color.data()
            );
            require(
                color[1] > color[0] + 30 && glGetError() == GL_NO_ERROR,
                "Table did not highlight the live row at this DPI."
            );
            std::string name = "truth-table-" + std::string(definition).substr(7) +
                               (scale == 2 ? "-2x.ppm" : ".ppm");
            saveImage(name.c_str());
            if (definition == BuiltinComponentIds::And)
            {
                ui.handleInput(
                    {UiInputKind::Cursor, 0, 0, 0, 0, area.x + 10, area.y + 10}, scene, input, frame
                );
                ui.handleInput({UiInputKind::Scroll, 0, 0, 0, 0, 0, 500}, scene, input, frame);
                draw();
                require(ui.truthTableScroll(scene) == 0, "Large table preview did not scroll.");
                name = scale == 2 ? "truth-table-eight-inputs-scrolled-2x.ppm"
                                  : "truth-table-eight-inputs-scrolled.ppm";
                saveImage(name.c_str());
            }
            input.setScene(nullptr);
        }
}

void customTruthTablePresentation(Renderer& renderer)
{
    for (int scale : {1, 2})
        for (bool clocked : {false, true})
        {
            Scene authored;
            const int data = authored.addComponent(BuiltinComponentIds::Input, {-12, 0});
            const int raw = authored.addComponent(BuiltinComponentIds::Output, {12, 2});
            const int inverted = authored.addComponent(BuiltinComponentIds::Output, {12, -2});
            authored.addComponent(BuiltinComponentIds::Not, {0, 0});
            require(
                static_cast<bool>(EditorActions(authored).apply(
                    {ConfigureComponentProperties{.componentId = data, .label = "Data"},
                     ConfigureComponentProperties{.componentId = raw, .label = "Raw"},
                     ConfigureComponentProperties{.componentId = inverted, .label = "Inverted"},
                     AddWire{{{-11, 0}, {-2, 0}}},
                     AddWire{{{-11, 0}, {-11, 2}, {11, 2}}},
                     AddWire{{{1, 0}, {11, 0}, {11, -2}}}}
                )),
                "Custom truth-table rendering fixture failed."
            );
            if (clocked)
                authored.addComponent(BuiltinComponentIds::Clock, {-20, 8});
            const auto definition =
                makeSubcircuit(authored, {"subcircuit.render-table", 1}, "Custom inverter");
            Scene scene;
            require(
                static_cast<bool>(
                    EditorActions(scene).apply({RegisterComponentDefinition{definition}})
                ),
                "Cannot register custom truth table for rendering."
            );
            const int id = scene.addComponent(definition.identity.id, {0, 0});
            scene.getLogicComponent(id)->setStateInPin(0, true);
            scene.markSimulationDirty();
            scene.propagate();
            scene.syncVisuals();
            Input input;
            input.setScene(&scene);
            UI ui;
            const CanvasSurface surface{extent / scale, extent / scale, extent, extent};
            ui.layout(scene.getComponentCatalog(), surface, input);
            CanvasCamera camera;
            camera.setViewport(input.getCanvasViewport());
            const auto frame = camera.frame(surface);
            const auto point = *frame.worldToWindow({0, 0});
            auto click = [&](CanvasViewport bounds, int button)
            {
                for (int action : {GLFW_PRESS, GLFW_RELEASE})
                    ui.handleInput(
                        {UiInputKind::MouseButton,
                         button,
                         action,
                         0,
                         0,
                         bounds.x + bounds.width / 2,
                         bounds.y + bounds.height / 2},
                        scene,
                        input,
                        frame
                    );
            };
            click({point.x, point.y, 0, 0}, GLFW_MOUSE_BUTTON_RIGHT);
            click(ui.infoTabBounds(scene, true), GLFW_MOUSE_BUTTON_LEFT);
            renderer.beginFrame(frame);
            const auto components = buildComponentPresentation(scene.getComponentViewMap());
            renderer.drawCanvas({components, scene.getWires(), {}});
            ui.draw(renderer, scene, frame);
            require(
                ui.truthTableOpen() && (ui.truthTable(scene).rows.empty() == clocked) &&
                    glGetError() == GL_NO_ERROR,
                "Custom table or unsupported explanation did not render."
            );
            if (!clocked)
            {
                require(
                    ui.truthTable(scene).headers ==
                        std::vector<std::string>{"Data", "Raw", "Inverted"},
                    "Custom table lost its named outputs."
                );
                const auto area = ui.truthTableRowsBounds(scene);
                std::array<unsigned char, 4> color{};
                glReadPixels(
                    static_cast<int>((area.x + 2) * scale),
                    extent - 1 - static_cast<int>((area.y + 22 + 10) * scale),
                    1,
                    1,
                    GL_RGBA,
                    GL_UNSIGNED_BYTE,
                    color.data()
                );
                require(
                    color[1] > color[0] + 30, "Custom table did not highlight the current row."
                );
            }
            const std::string name =
                std::string(clocked ? "truth-table-custom-clock" : "truth-table-custom") +
                (scale == 2 ? "-2x.ppm" : ".ppm");
            saveImage(name.c_str());
            input.setScene(nullptr);
        }
}

void liveSubcircuitPresentation(Renderer& renderer)
{
    for (int scale : {1, 2})
    {
        Scene authored;
        const int data = authored.addComponent(BuiltinComponentIds::Input, {-12, 2});
        const int clockPort = authored.addComponent(BuiltinComponentIds::Input, {-12, -2});
        const int flip = authored.addComponent(BuiltinComponentIds::DFlipFlop, {0, 0});
        const int q = authored.addComponent(BuiltinComponentIds::Output, {12, 2});
        const int notQ = authored.addComponent(BuiltinComponentIds::Output, {12, -2});
        authored.addComponent(BuiltinComponentIds::Clock, {0, 8});
        require(
            static_cast<bool>(EditorActions(authored).apply(
                {ConfigureComponentProperties{.componentId = data, .label = "Data"},
                 ConfigureComponentProperties{.componentId = clockPort, .label = "CLK"},
                 ConfigureComponentProperties{.componentId = q, .label = "Q"},
                 ConfigureComponentProperties{.componentId = notQ, .label = "~Q"},
                 AddWire{{{-11, 2}, {-3, 2}, {-3, 1}}},
                 AddWire{{{-11, -2}, {-3, -2}, {-3, -1}}},
                 AddWire{{{3, 1}, {11, 1}, {11, 2}}},
                 AddWire{{{3, -1}, {11, -1}, {11, -2}}}}
            )),
            "Live viewer render design failed."
        );
        const auto definition = makeSubcircuit(authored, {"subcircuit.render-live", 1}, "Register");
        CircuitViews views;
        views.publishSubcircuits({definition});
        auto& parent = views.activeScene();
        const int first = parent.addComponent(definition.identity.id, {-8, 0});
        parent.addComponent(definition.identity.id, {8, 0});
        parent.getLogicComponent(first)->setStateInPin(0, true);
        parent.getLogicComponent(first)->setStateInPin(1, true);
        parent.propagate();
        parent.stepAllClocks(1, &views.activeClockTimeline());
        parent.syncVisuals();
        views.markSaved(0);
        Input input;
        views.select(0, input);
        UI ui;
        ui.setCircuitViews(&views, &renderer.fontMetrics());
        const CanvasSurface surface{extent / scale, extent / scale, extent, extent};
        auto layout = [&]
        { ui.layout(views.displayedScene().getComponentCatalog(), surface, input); };
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
            views.refreshInspection(input);
            layout();
            const auto camera = frame();
            renderer.beginFrame(camera);
            const auto& scene = views.displayedScene();
            const auto components = buildComponentPresentation(scene.getComponentViewMap());
            const auto junctions = scene.getWireIntersections();
            renderer.drawCanvas({components, scene.getWires(), junctions});
            ui.draw(renderer, scene, camera);
        };
        auto click = [&](CanvasViewport bounds, int button)
        {
            for (int action : {GLFW_PRESS, GLFW_RELEASE})
            {
                ui.handleInput(
                    {UiInputKind::MouseButton,
                     button,
                     action,
                     0,
                     0,
                     bounds.x + bounds.width / 2,
                     bounds.y + bounds.height / 2},
                    views.displayedScene(),
                    input,
                    frame()
                );
                layout();
            }
        };
        layout();
        const auto position = *frame().worldToWindow(GridSystem::gridToWorld({-8, 0}));
        click({position.x, position.y, 0, 0}, GLFW_MOUSE_BUTTON_RIGHT);
        draw();
        require(ui.viewInsideBounds(parent).width > 0, "Placed circuit popup omitted View inside.");
        saveImage(scale == 1 ? "live-subcircuit-entry.ppm" : "live-subcircuit-entry-2x.ppm");
        click(ui.viewInsideBounds(parent), GLFW_MOUSE_BUTTON_LEFT);
        draw();
        require(
            views.inspecting() && input.isReadOnly() && views.size() == 1 &&
                views.displayedScene().getCommittedComponentView(flip)->getOutputPins()[0].state ==
                    PinState::ON,
            "Viewer rendered a new default simulation instead of the running instance."
        );
        const auto back = ui.inspectionBackBounds();
        std::array<unsigned char, 4> color{};
        glReadPixels(
            static_cast<int>((back.x + 2) * scale),
            extent - 1 - static_cast<int>((back.y + 2) * scale),
            1,
            1,
            GL_RGBA,
            GL_UNSIGNED_BYTE,
            color.data()
        );
        require(
            color[2] > 70 && glGetError() == GL_NO_ERROR,
            "Live Back bar did not render at this DPI."
        );
        saveImage(scale == 1 ? "live-subcircuit.ppm" : "live-subcircuit-2x.ppm");
        const auto inside = *frame().worldToWindow({0, 0});
        click({inside.x, inside.y, 0, 0}, GLFW_MOUSE_BUTTON_RIGHT);
        draw();
        require(
            ui.infoBounds(views.displayedScene()).y >=
                ui.inspectionBackBounds().y + ui.inspectionBackBounds().height,
            "Live component popup covered the Back strip."
        );
        const auto info = ui.componentInfo(views.displayedScene());
        require(
            std::find(info.begin(), info.end(), "Q: 1 (ON)") != info.end(),
            "Live popup rendered stale flip-flop state."
        );
        saveImage(
            scale == 1 ? "live-subcircuit-information.ppm" : "live-subcircuit-information-2x.ppm"
        );
        click(ui.inspectionBackBounds(), GLFW_MOUSE_BUTTON_LEFT);
        draw();
        require(
            !views.inspecting() && !input.isReadOnly() && !views.hasUnsavedChanges(0),
            "Back left a transient scene or unsaved viewer changes."
        );
        input.setScene(nullptr);
    }
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

void labelVisibilityPresentation(Renderer& renderer, const CanvasCameraFrame& camera)
{
    Scene scene;
    const int id = scene.addComponent(BuiltinComponentIds::Clock, {0, 0});
    const auto named = EditorActions(scene).apply({ConfigureComponentProperties{
        .componentId = id, .label = "REFERENCE", .labelVisible = true
    }});
    require(static_cast<bool>(named), "Cannot name the rendered clock.");
    auto draw = [&]()
    {
        const auto components = buildComponentPresentation(scene.getComponentViewMap());
        renderer.beginFrame(camera);
        renderer.drawCanvas({components, scene.getWires(), {}});
    };
    auto glyphs = [&]()
    {
        const float bottom = scene.getCommittedComponentView(id)->getBodyBounds().bottom;
        const int x = static_cast<int>((1 - 0.18f) * extent / 2);
        const int y = static_cast<int>((1 + bottom - 0.06f) * extent / 2);
        const int width = static_cast<int>(0.36f * extent / 2);
        const int height = static_cast<int>(0.052f * extent / 2);
        std::vector<unsigned char> values(width * height * 4);
        glReadPixels(x, y, width, height, GL_RGBA, GL_UNSIGNED_BYTE, values.data());
        int count = 0;
        for (std::size_t i = 0; i < values.size(); i += 4)
            count += values[i] > 180 && values[i + 1] > 180 && values[i + 2] > 180;
        return count;
    };
    draw();
    require(glyphs() > 15, "Enabled clock label did not render on the canvas.");
    saveImage("clock-label-visible.ppm");
    const auto hidden = EditorActions(scene).apply(
        {ConfigureComponentProperties{.componentId = id, .labelVisible = false}}
    );
    require(static_cast<bool>(hidden), "Cannot hide the rendered clock label.");
    draw();
    require(
        glyphs() == 0 && scene.clockName({id}) == "REFERENCE",
        "Hidden canvas label still rendered or erased the clock's name."
    );
    saveImage("clock-label-hidden.ppm");
}

void examplesPresentation(Renderer& renderer)
{
    for (const int dpi : {1, 2})
    {
        const CanvasSurface surface{extent / dpi, extent / dpi, extent, extent};
        CircuitViews views;
        Input input;
        UI ui;
        views.select(0, input);
        ui.setCircuitViews(&views, &renderer.fontMetrics());
        for (const auto& example : circuitExamples())
        {
            views.create(input);
            views.replaceWorkspace(
                buildExampleCircuit(example.id), std::string(example.name), input
            );
            input.setMode(EditorMode::Interaction);
            ui.layout(views.activeScene().getComponentCatalog(), surface, input);
            if (example.id == CircuitExample::Counter)
            {
                ui.showClockPanel(input);
                views.activeClockTimeline().record(0, views.activeScene().clockReadings());
            }
            input.executeViewCommand(Input::ViewCommand::FitCircuit, surface);
            ui.setFileStatus(std::string(example.description));
            CanvasCamera camera;
            camera.setViewport(input.getCanvasViewport());
            camera.setCenter(input.getPanOffset());
            camera.setZoom(input.getZoom());
            const auto frame = camera.frame(surface);
            auto draw = [&]
            {
                renderer.beginFrame(frame);
                const auto data =
                    buildComponentPresentation(views.activeScene().getComponentViewMap());
                renderer.drawCanvas(
                    {data,
                     views.activeScene().getWires(),
                     views.activeScene().getWireIntersections()}
                );
                ui.draw(renderer, views.activeScene(), frame);
            };
            draw();
            for (const auto& [id, view] : views.activeScene().getComponentViewMap())
            {
                const auto point = *frame.worldToWindow(view->getPosition());
                require(
                    frame.viewport.contains(point.x, point.y),
                    "Example did not fit into the canvas."
                );
            }
            const std::string name = "example-" + std::to_string(static_cast<int>(example.id)) +
                                     (dpi == 1 ? ".ppm" : "-2x.ppm");
            saveImage(name.c_str());
            if (example.id != CircuitExample::HalfAdder)
                continue;
            const auto file = ui.fileBounds();
            for (const auto action : {GLFW_PRESS, GLFW_RELEASE})
                ui.handleInput(
                    {UiInputKind::MouseButton,
                     GLFW_MOUSE_BUTTON_LEFT,
                     action,
                     0,
                     0,
                     file.x + file.width / 2,
                     file.y + file.height / 2},
                    views.activeScene(),
                    input,
                    frame
                );
            const auto parent = ui.fileOptionBounds(static_cast<int>(ui.fileCommands().size()));
            UiInputEvent hover{UiInputKind::Cursor};
            hover.x = parent.x + parent.width / 2;
            hover.y = parent.y + parent.height / 2;
            ui.handleInput(hover, views.activeScene(), input, frame);
            require(
                ui.examplesMenuOpen(), "Examples submenu did not expand in the rendering fixture."
            );
            draw();
            const auto menu = ui.examplesMenuBounds();
            std::array<unsigned char, 4> value{};
            glReadPixels(
                static_cast<int>((menu.x + 3) * dpi),
                extent - 1 - static_cast<int>((menu.y + 3) * dpi),
                1,
                1,
                GL_RGBA,
                GL_UNSIGNED_BYTE,
                value.data()
            );
            require(
                value[2] > 30 && value[2] > value[0], "Examples submenu was clipped or missing."
            );
            saveImage(dpi == 1 ? "examples-menu.ppm" : "examples-menu-2x.ppm");
            ui.dismissPopups(input);
        }
        ui.setCircuitViews(nullptr, nullptr);
        input.setScene(nullptr);
    }
}

void navigationMenuPresentation(Renderer& renderer)
{
    for (int scale : {1, 2})
    {
        Scene scene;
        scene.addComponent(BuiltinComponentIds::And, {0, 0});
        Input input;
        input.setScene(&scene);
        UI ui;
        const CanvasSurface surface{extent / scale, extent / scale, extent, extent};
        ui.layout(scene.getComponentCatalog(), surface, input);
        CanvasCamera camera;
        camera.setViewport(input.getCanvasViewport());
        const auto frame = camera.frame(surface);
        for (const auto menu : {UI::Menu::Edit, UI::Menu::View})
        {
            ui.dismissPopups(input);
            const auto bounds = ui.menuBounds(menu);
            for (const auto action : {GLFW_PRESS, GLFW_RELEASE})
                ui.handleInput(
                    {UiInputKind::MouseButton,
                     GLFW_MOUSE_BUTTON_LEFT,
                     action,
                     0,
                     0,
                     bounds.x + bounds.width / 2,
                     bounds.y + bounds.height / 2},
                    scene,
                    input,
                    frame
                );
            require(ui.menuOpen(menu), "Navigation dropdown rendering fixture did not open.");
            renderer.beginFrame(frame);
            const auto data = buildComponentPresentation(scene.getComponentViewMap());
            renderer.drawCanvas({data, scene.getWires(), {}});
            ui.draw(renderer, scene, frame);
            const auto option = ui.menuOptionBounds(menu, 0);
            std::array<unsigned char, 4> value{};
            glReadPixels(
                static_cast<int>((option.x + 3) * scale),
                extent - 1 - static_cast<int>((option.y + 3) * scale),
                1,
                1,
                GL_RGBA,
                GL_UNSIGNED_BYTE,
                value.data()
            );
            require(value[2] > 30, "Navigation menu background did not render above the canvas.");
            const auto name = std::string(menu == UI::Menu::Edit ? "edit-menu" : "view-menu") +
                              (scale == 2 ? "-2x.ppm" : ".ppm");
            saveImage(name.c_str());
        }
        input.setScene(nullptr);
    }
}

void gateSettingsPresentation(Renderer& renderer)
{
    Scene scene;
    Input input;
    input.setScene(&scene);
    UI ui;
    const int id = scene.addComponent(BuiltinComponentIds::And, {-5, 0});
    for (int scale : {1, 2})
    {
        const CanvasSurface surface{extent / scale, extent / scale, extent, extent};
        ui.layout(scene.getComponentCatalog(), surface, input);
        CanvasCamera camera;
        camera.setViewport(input.getCanvasViewport());
        const auto frame = camera.frame(surface);
        const auto point = *frame.worldToWindow(GridSystem::gridToWorld({-5, 0}));
        auto event = [&](int button, int action, double x, double y)
        {
            return ui.handleInput(
                {UiInputKind::MouseButton, button, action, 0, 0, x, y}, scene, input, frame
            );
        };
        event(GLFW_MOUSE_BUTTON_RIGHT, GLFW_PRESS, point.x, point.y);
        event(GLFW_MOUSE_BUTTON_RIGHT, GLFW_RELEASE, point.x, point.y);
        require(ui.infoComponentId() == id, "Gate settings rendering fixture did not open.");
        auto click = [&](CanvasViewport bounds)
        {
            event(
                GLFW_MOUSE_BUTTON_LEFT,
                GLFW_PRESS,
                bounds.x + bounds.width / 2,
                bounds.y + bounds.height / 2
            );
            event(
                GLFW_MOUSE_BUTTON_LEFT,
                GLFW_RELEASE,
                bounds.x + bounds.width / 2,
                bounds.y + bounds.height / 2
            );
        };
        auto draw = [&](bool popup = true)
        {
            scene.propagate();
            scene.syncVisuals();
            const auto components = buildComponentPresentation(scene.getComponentViewMap());
            const auto junctions = scene.getWireIntersections();
            renderer.beginFrame(frame);
            renderer.drawCanvas({components, scene.getWires(), junctions});
            if (popup)
                ui.draw(renderer, scene, frame);
        };
        auto screenPixel = [&](CanvasViewport bounds)
        {
            std::array<unsigned char, 4> value{};
            glReadPixels(
                static_cast<int>((bounds.x + 3) * scale),
                extent - 1 - static_cast<int>((bounds.y + 3) * scale),
                1,
                1,
                GL_RGBA,
                GL_UNSIGNED_BYTE,
                value.data()
            );
            return value;
        };
        draw();
        const auto plus = ui.gateInputButtonBounds(scene, true);
        const auto active = screenPixel(plus);
        require(active[2] > 65, "Gate input settings button did not render.");
        click(plus);
        click(ui.gateInversionBounds(scene));
        click(ui.gateInputInversionBounds(scene, 0));
        draw(false);
        const auto* gate = static_cast<const Gate*>(scene.getLogicComponent(id));
        require(
            gate->getInputPinCount() == 3 && gate->isInverted() &&
                ui.componentInfo(scene).front() == "NAND",
            "Rendered gate controls did not update the logic/type."
        );
        for (const auto& pin : scene.getCommittedComponentView(id)->getInputPins())
        {
            const auto value = cameraPixel(
                frame, scene.getCommittedComponentView(id)->getAbsolutePinWorldPos(pin)
            );
            require(
                value[0] > 200 || value[2] > 200,
                "Resized gate input pin did not render at its new anchor."
            );
        }
        draw();
        saveImage(scale == 1 ? "gate-settings.ppm" : "gate-settings-2x.ppm");
        click(ui.componentOrientationBounds(scene, Orientation::Up));
        draw();
        require(
            scene.getCommittedComponentView(id)->getOrientation() == Orientation::Up,
            "Rendered orientation control did not update the gate."
        );
        saveImage(scale == 1 ? "gate-orientation-popup.ppm" : "gate-orientation-popup-2x.ppm");
        click(ui.componentOrientationBounds(scene, Orientation::Right));
        input.setMode(EditorMode::Interaction);
        ui.layout(scene.getComponentCatalog(), surface, input);
        draw();
        const auto disabled = screenPixel(ui.gateInputButtonBounds(scene, true));
        require(disabled[2] < active[2] - 10, "Interaction settings did not look disabled.");
        saveImage(scale == 1 ? "gate-settings-readonly.ppm" : "gate-settings-readonly-2x.ppm");
        input.setMode(EditorMode::Selection);
        ui.layout(scene.getComponentCatalog(), surface, input);
        require(
            static_cast<bool>(EditorActions(scene).apply(
                {ConfigureComponentProperties{.componentId = id, .inputCount = 8}}
            )),
            "Cannot expand popup inputs."
        );
        draw();
        const auto inputList = ui.gateInputListBounds(scene);
        const auto listColor = screenPixel(inputList);
        require(
            listColor[0] < 18 && listColor[2] < 38,
            "Input list did not render its subtle background."
        );
        saveImage(scale == 1 ? "gate-input-list.ppm" : "gate-input-list-2x.ppm");
        ui.handleInput(
            {UiInputKind::Cursor, 0, 0, 0, 0, inputList.x + 10, inputList.y + 10},
            scene,
            input,
            frame
        );
        ui.handleInput({UiInputKind::Scroll, 0, 0, 0, 0, 0, -20}, scene, input, frame);
        draw();
        require(
            ui.gateInputInversionBounds(scene, 7).width > 0 &&
                ui.gateInputInversionBounds(scene, 0).width == 0,
            "Rendered list did not scroll to the last input."
        );
        saveImage(scale == 1 ? "gate-input-list-scrolled.ppm" : "gate-input-list-scrolled-2x.ppm");
        require(
            static_cast<bool>(EditorActions(scene).apply(
                {ConfigureComponentProperties{.componentId = id, .inputCount = 3}}
            )),
            "Cannot restore popup input count."
        );
        click(ui.gateInputInversionBounds(scene, 0));
        click(ui.gateInversionBounds(scene));
        click(ui.gateInputButtonBounds(scene, false));
        require(
            scene.getLogicComponent(id)->getInputPinCount() == 2, "Rendered shrink control failed."
        );
        ui.dismissPopups(input);
    }
    require(glGetError() == GL_NO_ERROR, "Gate settings drawing caused an OpenGL error.");
    input.setScene(nullptr);
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

void groupSelectionPresentation(Renderer& renderer, const CanvasCameraFrame& camera)
{
    Scene scene;
    const int a = scene.addComponent(BuiltinComponentIds::Not, {-6, 0});
    const int b = scene.addComponent(BuiltinComponentIds::Not, {6, 0});
    require(
        static_cast<bool>(EditorActions(scene).apply({AddWire{{{-5, 0}, {4, 0}}}})),
        "Group rendering fixture failed."
    );
    scene.propagate();
    scene.syncVisuals();
    const auto components = buildComponentPresentation(scene.getComponentViewMap());
    const auto junctions = scene.getWireIntersections();
    std::vector<BodyHighlight> selected{
        {scene.getCommittedComponentView(a)->getBodyBounds(), 1},
        {scene.getCommittedComponentView(b)->getBodyBounds(), 1}
    };
    std::vector<SegmentHighlight> wires{{{-5, 0}, {4, 0}, 0.35f}};
    CanvasFrame frame{components, scene.getWires(), junctions};
    frame.selectedBodies = selected;
    frame.selectedSegments = wires;
    auto draw = [&](const CanvasCameraFrame& view)
    {
        renderer.beginFrame(view);
        renderer.drawCanvas(frame);
    };
    draw(camera);
    const auto bounds = selected.front().bounds;
    glm::vec2 edge{bounds.centerX(), bounds.top + 0.01f};
    unsigned char strong = 0;
    for (int i = -4; i <= 4; ++i)
    {
        const glm::vec2 sample{edge.x, bounds.top + 0.01f + i * 2.0f / extent};
        const auto red = pixel(sample)[0];
        if (red > strong)
        {
            strong = red;
            edge = sample;
        }
    }
    frame.selectedBodies = {};
    draw(camera);
    const auto background = pixel(edge)[0];
    for (auto& body : selected)
        body.opacity = 0.35f;
    frame.selectedBodies = selected;
    draw(camera);
    const auto faint = pixel(edge)[0];
    require(
        strong > 180 && faint > background + 25 && faint + 60 < strong,
        "Live selection preview was missing or as strong as committed highlights."
    );
    for (auto& body : selected)
        body.opacity = 1.0f;
    frame.selectedBodies = selected;
    wires.front().opacity = 1.0f;
    draw(camera);
    require(pixel(edge)[0] == strong, "Released selection did not restore solid outlines.");
    saveImage("group-selection-committed.ppm");
    for (auto& body : selected)
        body.opacity = 0.35f;
    wires.front().opacity = 0.35f;
    draw(camera);
    saveImage("group-selection.ppm");
    const auto without = pixel({0, 0.12f});
    frame.selectionBox = BodyBounds{-0.5f, -0.25f, 0.5f, 0.25f};
    draw(camera);
    const auto withBox = pixel({0, 0.12f});
    require(
        withBox[2] > without[2] + 5 && withBox[2] < without[2] + 25,
        "Selection rectangle fill was missing or hid the canvas."
    );
    saveImage("box-selection.ppm");
    CanvasCamera zoomed;
    zoomed.setZoom(0.8f);
    zoomed.setCenter({0.08f, -0.05f});
    draw(zoomed.frame({512, 512, extent, extent}));
    saveImage("box-selection-2x.ppm");
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
    const auto firstBottomTab = ui.bottomTabBounds(UI::BottomTab::Messages);
    require(
        bottom.x == mainFrame.viewport.x &&
            bottom.y == mainFrame.viewport.y + mainFrame.viewport.height &&
            screenPixel(firstBottomTab.x + 3, firstBottomTab.y + 3)[2] > 100,
        "Bottom panel was not reserved beneath the canvas or its active tab was not drawn."
    );
    click(ui.bottomTabBounds(UI::BottomTab::Messages), GLFW_MOUSE_BUTTON_LEFT);
    draw();
    saveImage("bottom-panel.ppm");
    ui.setFileStatus("Cannot open circuit: the file is not valid JSON.", true);
    ui.addMessage("No safe wire route; choose a clear endpoint.");
    draw();
    const auto messageBox = ui.messageBoxBounds();
    const auto messageBackground = screenPixel(messageBox.x + 2, messageBox.y + 2);
    std::vector<unsigned char> messagePixels(extent * extent * 4);
    glReadPixels(0, 0, extent, extent, GL_RGBA, GL_UNSIGNED_BYTE, messagePixels.data());
    auto messageInk = [&](CanvasViewport area, bool error)
    {
        int count = 0;
        for (int y = static_cast<int>(area.y); y < area.y + area.height; ++y)
            for (int x = static_cast<int>(area.x); x < area.x + area.width; ++x)
            {
                const int px = static_cast<int>(x * extent / surface.windowWidth);
                const int py = extent - 1 - static_cast<int>(y * extent / surface.windowHeight);
                const auto* color = messagePixels.data() + (py * extent + px) * 4;
                if (error ? color[0] > 150 && color[0] > color[1] + 40 && color[1] > color[2] + 15
                          : color[0] > 180 && color[1] > 180 && color[2] > 180)
                    ++count;
            }
        return count;
    };
    require(
        messageBackground[2] < 30 && messageInk(messageBox, true) > 30 &&
            screenPixel(bottom.x + bottom.width - 12, bottom.y + 12)[2] < 40,
        "Workspace Messages did not draw its errors or retained the removed Clear button."
    );
    saveImage("messages-workspace.ppm");
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
    ui.addMessage("Cannot publish draft: name every input and output before saving.");
    require(
        ui.hasUnreadMessages() && ui.activeBottomTab() == UI::BottomTab::Subcircuit,
        "Subcircuit error opened Messages instead of marking its tab."
    );
    draw();
    const auto hiddenMessages = ui.bottomTabBounds(UI::BottomTab::Messages);
    const auto alertColor =
        screenPixel(hiddenMessages.x + 4, hiddenMessages.y + hiddenMessages.height - 1);
    require(
        ui.messagesTabLabel() == "Messages (1)" && alertColor[0] > alertColor[1] + 60,
        "Inactive Messages tab did not show a persistent red error accent/count."
    );
    saveImage("messages-action-alert.ppm");
    click(ui.messageNoticeBounds(), GLFW_MOUSE_BUTTON_LEFT);
    draw();
    const auto add = ui.circuitTabs().addBounds();
    const auto addFill = screenPixel(add.x + 3, add.y + 3);
    require(
        add.width == add.height && addFill[2] > 45 && addFill[0] < 40,
        "Trailing plus square did not draw separately from the circuit tabs."
    );
    require(
        ui.messages().size() == 1 && !ui.hasUnreadMessages(),
        "Subcircuit Messages inherited workspace errors or retained its unread mark."
    );
    saveImage("messages-subcircuit.ppm");
    ui.clearMessages();
    click(ui.bottomTabBounds(UI::BottomTab::Subcircuit), GLFW_MOUSE_BUTTON_LEFT);
    const int unsettled = views.activeScene().addComponent(BuiltinComponentIds::Not, {-20, 10});
    const auto loop = EditorActions(views.activeScene())
                          .apply({AddWire{{{-19, 10}, {-19, 14}, {-22, 14}, {-22, 10}}}});
    require(static_cast<bool>(loop), "FIND presentation fixture failed.");
    views.activeScene().propagate();
    ui.reportSimulation(views.activeScene(), 0);
    draw();
    require(
        ui.activeBottomTab() == UI::BottomTab::Subcircuit &&
            ui.messagesTabLabel() == "Messages (1)",
        "A new error automatically switched tabs."
    );
    saveImage("messages-error-alert.ppm");
    click(ui.messageNoticeBounds(), GLFW_MOUSE_BUTTON_LEFT);
    draw();
    const auto find = ui.findMessageBounds(0);
    require(
        find.width > 0 && ui.simulationIssues()[0].location == GridCoords{-20, 10},
        "Active simulation problem did not present its location and FIND link."
    );
    const double errorEnd =
        ui.messageBoxBounds().x + 10 +
        getTextWidth(ui.simulationIssues()[0].message, 0.4f, renderer.fontMetrics());
    const double spacing = getTextWidth(" ", 0.4f, renderer.fontMetrics());
    const auto underline = screenPixel(find.x + find.width / 2, find.y + find.height - 2);
    require(
        std::abs(find.x - errorEnd - spacing) < 0.01 && underline[2] > underline[0] + 50,
        "FIND was not drawn and clickable immediately after the error text."
    );
    saveImage("messages-find.ppm");
    click(find, GLFW_MOUSE_BUTTON_LEFT);
    require(
        input.getPanOffset() == GridSystem::gridToWorld({-20, 10}),
        "Rendered FIND did not center its error location."
    );
    draw();
    saveImage("messages-found.ppm");
    const auto originalSurface = surface;
    surface = {512, 512, extent, extent};
    draw();
    const auto smallNotice = ui.messageNoticeBounds();
    require(
        smallNotice.width > 0 && ui.messageNoticeText() == "See Messages",
        "Narrow layout did not provide a readable compact error notice."
    );
    click(ui.bottomTabBounds(UI::BottomTab::Subcircuit), GLFW_MOUSE_BUTTON_LEFT);
    draw();
    saveImage("messages-error-alert-2x.ppm");
    click(smallNotice, GLFW_MOUSE_BUTTON_LEFT);
    draw();
    require(ui.findMessageBounds(0).width > 0, "Narrow 2x layout lost its FIND link.");
    saveImage("messages-find-2x.ppm");
    surface = originalSurface;
    views.activeScene().removeComponent(unsettled);
    views.activeScene().propagate();
    ui.reportSimulation(views.activeScene(), 1);
    input.setPanOffset({0, 0});
    draw();
    require(
        ui.simulationIssues().empty() && ui.messages().empty(),
        "Repaired circuit kept an old error instead of the healthy status."
    );
    saveImage("messages-healthy.ppm");
    views.activeMessages().issues.push_back(
        {"Simulation falling behind real time.", std::nullopt, true}
    );
    views.activeMessages().behind = true;
    click(ui.bottomTabBounds(UI::BottomTab::Subcircuit), GLFW_MOUSE_BUTTON_LEFT);
    draw();
    const auto warningTab = ui.bottomTabBounds(UI::BottomTab::Messages);
    const auto warningColor = screenPixel(warningTab.x + 4, warningTab.y + warningTab.height - 1);
    require(
        !ui.hasMessageErrors() && warningColor[0] > 180 && warningColor[1] > 140 &&
            warningColor[2] < 130,
        "Warning-only Messages did not use an amber accent."
    );
    saveImage("messages-warning-alert.ppm");
    views.activeMessages().issues.clear();
    views.activeMessages().behind = false;
    click(ui.bottomTabBounds(UI::BottomTab::Messages), GLFW_MOUSE_BUTTON_LEFT);
    draw();
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
    const auto dpiBottomTab = ui.bottomTabBounds(UI::BottomTab::Messages);
    require(
        screenPixel(dpiBottomTab.x + 3, dpiBottomTab.y + 3)[2] > 100,
        "Bottom panel tab did not follow 2x display scaling."
    );
    saveImage("bottom-panel-2x.ppm");
    require(
        dpiBottomTab.x + dpiBottomTab.width <= ui.bottomBounds().x + ui.bottomBounds().width - 8,
        "Messages tab overflowed the narrow 2x layout."
    );
    saveImage("messages-subcircuit-2x.ppm");
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
    views.markSaved(1);
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
    const auto closeRequest = ui.takeCloseCircuit();
    require(closeRequest == 1, "Confirmed tab deletion was not queued for unsaved protection.");
    views.remove(*closeRequest, input);
    input.setUiCapture({});
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
        ui.fileOptions().size() == 5 && ui.fileOptions()[0] == "Save to library",
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
    for (const int scale : {1, 2})
    {
        surface = {extent / scale, extent / scale, extent, extent};
        draw();
        click(ui.menuBounds(UI::Menu::Simulation), GLFW_MOUSE_BUTTON_LEFT);
        draw();
        const auto simulationMenu = ui.dropdownBounds(UI::Menu::Simulation);
        require(
            ui.menuOpen(UI::Menu::Simulation) &&
                screenPixel(simulationMenu.x + 3, simulationMenu.y + 3)[2] > 30,
            "Simulation dropdown did not render at this display scale."
        );
        saveImage(scale == 1 ? "simulation-menu.ppm" : "simulation-menu-2x.ppm");
        ui.dismissPopups(input);
    }
    ui.setLibraryDrafts({{"subcircuit.draft", "Learning circuit", {}, 0, 0, "draft.json"}});
    for (const int scale : {1, 2})
    {
        surface = {extent / scale, extent / scale, extent, extent};
        draw();
        click(ui.tabBounds(UI::Tab::Custom), GLFW_MOUSE_BUTTON_LEFT);
        draw();
        saveImage(scale == 1 ? "library-drafts.ppm" : "library-drafts-2x.ppm");
        ui.showUnsavedPrompt(
            {"Main", "Adder", "Learning circuit", "Workspace", "Draft"}, true, input
        );
        draw();
        const auto unsavedBox = ui.unsavedPromptBounds();
        const auto save = ui.unsavedButtonBounds(UI::UnsavedChoice::Save);
        require(
            screenPixel(unsavedBox.x + 2, unsavedBox.y + 2)[2] > 25 &&
                screenPixel(save.x + 3, save.y + 3)[2] > 80,
            "Unsaved-work prompt or Save All button did not render above the UI."
        );
        saveImage(scale == 1 ? "unsaved-work.ppm" : "unsaved-work-2x.ppm");
        click(ui.unsavedButtonBounds(UI::UnsavedChoice::Cancel), GLFW_MOUSE_BUTTON_LEFT);
        require(
            ui.takeUnsavedChoice() == UI::UnsavedChoice::Cancel, "Rendered Cancel did not dispatch."
        );
        input.setUiCapture({});
    }
    ui.dismissPopups(input);
    ui.setCircuitViews(nullptr, nullptr);
    input.setScene(nullptr);
}

void bottomResizePresentation(Renderer& renderer)
{
    for (int dpi : {1, 2})
    {
        CircuitViews views;
        Input input;
        views.select(0, input);
        for (int i = 0; i < 4; ++i)
            views.activeScene().addComponent(BuiltinComponentIds::Clock, {i * 6, 0});
        views.activeScene().stepAllClocks(1, &views.activeClockTimeline());
        views.activeScene().stepAllClocks(2, &views.activeClockTimeline());
        UI ui;
        ui.setCircuitViews(&views, &renderer.fontMetrics());
        const CanvasSurface surface{extent / dpi, extent / dpi, extent, extent};
        ui.layout(views.activeScene().getComponentCatalog(), surface, input);
        CanvasCamera camera;
        camera.setViewport(input.getCanvasViewport());
        auto frame = camera.frame(surface);
        UiInputEvent event{UiInputKind::MouseButton};
        event.code = GLFW_MOUSE_BUTTON_LEFT;
        const auto tab = ui.bottomTabBounds(UI::BottomTab::Clocks);
        event.x = tab.x + tab.width / 2;
        event.y = tab.y + tab.height / 2;
        event.action = GLFW_PRESS;
        ui.handleInput(event, views.activeScene(), input, frame);
        event.action = GLFW_RELEASE;
        ui.handleInput(event, views.activeScene(), input, frame);
        const auto grip = ui.bottomResizeBounds();
        event.x = grip.x + grip.width / 2;
        event.y = grip.y + 3;
        event.action = GLFW_PRESS;
        ui.handleInput(event, views.activeScene(), input, frame);
        event.kind = UiInputKind::Cursor;
        event.y -= 80;
        ui.handleInput(event, views.activeScene(), input, frame);
        camera.setViewport(input.getCanvasViewport());
        frame = camera.frame(surface);
        renderer.beginFrame(frame);
        const auto components =
            buildComponentPresentation(views.activeScene().getComponentViewMap());
        renderer.drawCanvas({components, views.activeScene().getWires(), {}});
        ui.draw(renderer, views.activeScene(), frame);
        const auto resized = ui.bottomResizeBounds();
        std::array<unsigned char, 4> color;
        glReadPixels(
            static_cast<int>((resized.x + resized.width / 2) * dpi),
            extent - 1 - static_cast<int>((resized.y + 2) * dpi),
            1,
            1,
            GL_RGBA,
            GL_UNSIGNED_BYTE,
            color.data()
        );
        require(
            color[2] > 200 && color[2] > color[0] + 80 &&
                frame.viewport.y + frame.viewport.height == ui.bottomBounds().y &&
                ui.clockListBounds().height >= 160,
            "Resized panel lost its visible grip, canvas boundary, or expanded clock rows."
        );
        saveImage(dpi == 1 ? "bottom-resized.ppm" : "bottom-resized-2x.ppm");
        event.kind = UiInputKind::MouseButton;
        event.action = GLFW_RELEASE;
        ui.handleInput(event, views.activeScene(), input, frame);
    }
}

void clockPresentation(Renderer& renderer)
{
    CircuitViews views;
    Input input;
    views.select(0, input);
    views.activeScene().addComponent(BuiltinComponentIds::Clock, {0, 0});
    views.activeScene().stepAllClocks(1, &views.activeClockTimeline());
    views.activeScene().stepAllClocks(2, &views.activeClockTimeline());
    UI ui;
    ui.setCircuitViews(&views, &renderer.fontMetrics());
    for (int dpi : {1, 2})
    {
        const CanvasSurface surface{extent / dpi, extent / dpi, extent, extent};
        ui.layout(views.activeScene().getComponentCatalog(), surface, input);
        CanvasCamera camera;
        camera.setViewport(input.getCanvasViewport());
        const auto frame = camera.frame(surface);
        const auto tab = ui.bottomTabBounds(UI::BottomTab::Clocks);
        UiInputEvent click{UiInputKind::MouseButton};
        click.code = GLFW_MOUSE_BUTTON_LEFT;
        click.action = GLFW_PRESS;
        click.x = tab.x + tab.width / 2;
        click.y = tab.y + tab.height / 2;
        ui.handleInput(click, views.activeScene(), input, frame);
        click.action = GLFW_RELEASE;
        ui.handleInput(click, views.activeScene(), input, frame);
        renderer.beginFrame(frame);
        ui.draw(renderer, views.activeScene(), frame);
        const auto list = ui.clockListBounds();
        const double labelWidth =
            std::min(220.0, std::min(list.width * 0.65, std::max(160.0, list.width * 0.4)));
        const double graphX = list.x + labelWidth, graphWidth = list.width - labelWidth;
        auto sample = [&](double time, double rowY)
        {
            std::array<unsigned char, 4> color;
            glReadPixels(
                static_cast<int>(
                    (graphX + (time - ui.clockStartTime()) / ui.clockWindowSeconds() * graphWidth) *
                    dpi
                ),
                extent - 1 - static_cast<int>((list.y + rowY) * dpi),
                1,
                1,
                GL_RGBA,
                GL_UNSIGNED_BYTE,
                color.data()
            );
            return color;
        };
        require(
            sample(0.25, 35)[1] > 160 && sample(0.75, 9)[1] > 160 && sample(0.5, 22)[1] > 160 &&
                sample(0.25, 9)[1] < 80,
            "Clock diagram omitted digital levels/edges or ignored DPI."
        );
        const double now = views.activeClockTimeline().time();
        require(
            sample(now, 5)[2] > 150 &&
                std::abs((now - ui.clockStartTime()) / ui.clockWindowSeconds() - 0.5) < 1e-9,
            "The current-time marker is missing from the centre of the clock graph."
        );
        const auto future = sample(2.0, 10);
        require(
            future[1] > 65 && future[1] < 160 && future[1] > future[0] + 15,
            "Clock preview edges were missing or looked like recorded history."
        );
        const auto clear = ui.clockControlBounds(UI::ClockControl::Clear);
        require(
            clear.x + clear.width == ui.bottomBounds().x + ui.bottomBounds().width - 8,
            "Clock toolbar overflowed a narrow window."
        );
        saveImage(dpi == 1 ? "clock-panel.ppm" : "clock-panel-2x.ppm");
        auto graphRegion = [&](bool future)
        {
            const int width = std::max(1, static_cast<int>((graphWidth / 2 - 4) * dpi));
            const int height = 40 * dpi;
            std::vector<unsigned char> pixels(width * height * 4);
            glReadPixels(
                static_cast<int>((graphX + (future ? graphWidth / 2 + 2 : 0)) * dpi),
                extent - static_cast<int>((list.y + 44) * dpi),
                width,
                height,
                GL_RGBA,
                GL_UNSIGNED_BYTE,
                pixels.data()
            );
            return pixels;
        };
        const auto past = graphRegion(false), forecast = graphRegion(true);
        const auto window = ui.clockWindowSeconds(), start = ui.clockStartTime();
        for (const float hz : {0.5f, 256.0f})
        {
            require(
                static_cast<bool>(EditorActions(views.activeScene())
                                      .apply({ConfigureComponentProperties{
                                          .componentId = 0, .clockFrequency = hz
                                      }})),
                "Cannot change the preview frequency."
            );
            ui.layout(views.activeScene().getComponentCatalog(), surface, input);
            renderer.beginFrame(frame);
            ui.draw(renderer, views.activeScene(), frame);
            require(
                ui.clockWindowSeconds() == window && ui.clockStartTime() == start &&
                    graphRegion(false) == past && graphRegion(true) != forecast,
                "Changing Hz rescaled/redrew past history or left the old future preview."
            );
            if (hz == 0.5f)
                saveImage(
                    dpi == 1 ? "clock-preview-rate-change.ppm" : "clock-preview-rate-change-2x.ppm"
                );
        }
        require(
            static_cast<bool>(
                EditorActions(views.activeScene())
                    .apply({ConfigureComponentProperties{.componentId = 0, .clockFrequency = 1.0f}})
            ),
            "Cannot restore the original clock frequency."
        );
        renderer.beginFrame(frame);
        ui.draw(renderer, views.activeScene(), frame);
    }
    const auto list = ui.clockListBounds();
    const double labelWidth =
        std::min(220.0, std::min(list.width * 0.65, std::max(160.0, list.width * 0.4)));
    const double graphX = list.x + labelWidth, graphWidth = list.width - labelWidth;
    auto colorAt = [&](double x, double y)
    {
        std::array<unsigned char, 4> color;
        glReadPixels(
            static_cast<int>(x * 2),
            extent - 1 - static_cast<int>(y * 2),
            1,
            1,
            GL_RGBA,
            GL_UNSIGNED_BYTE,
            color.data()
        );
        return color;
    };
    const double cursor = graphX + graphWidth / 2;
    require(
        colorAt(cursor, list.y + 45)[2] < 65,
        "Clock cursor/edge guide extended into the gap between rows."
    );
    views.activeScene().removeComponent(0);
    renderer.beginFrame(CanvasCamera{}.frame({extent / 2, extent / 2, extent, extent}));
    ui.draw(
        renderer,
        views.activeScene(),
        CanvasCamera{}.frame({extent / 2, extent / 2, extent, extent})
    );
    require(colorAt(cursor, list.y + 10)[2] < 65, "No-clock panel retained a time cursor.");
    saveImage("clock-panel-empty.ppm");

    const int fast = views.activeScene().addComponent(
        BuiltinComponentIds::Clock, {0, 0}, ComponentOverrides{.clockFrequency = 1000.0f}
    );
    auto& scene = views.activeScene();
    scene.togglePauseAllClocks(); // Resume the previously stepped scene.
    scene.updateClocks(1, &views.activeClockTimeline());
    while (scene.pendingClockTime() > 0)
        scene.updateClocks(0, &views.activeClockTimeline());
    auto clickControl = [&](UI::ClockControl control)
    {
        const auto box = ui.clockControlBounds(control);
        UiInputEvent click{UiInputKind::MouseButton};
        click.code = GLFW_MOUSE_BUTTON_LEFT;
        click.x = box.x + box.width / 2;
        click.y = box.y + box.height / 2;
        click.action = GLFW_PRESS;
        ui.handleInput(
            click, scene, input, CanvasCamera{}.frame({extent / 2, extent / 2, extent, extent})
        );
        click.action = GLFW_RELEASE;
        ui.handleInput(
            click, scene, input, CanvasCamera{}.frame({extent / 2, extent / 2, extent, extent})
        );
    };
    clickControl(UI::ClockControl::Fit);
    for (int i = 0; i < 11; ++i)
        clickControl(UI::ClockControl::ZoomOut);
    renderer.beginFrame(CanvasCamera{}.frame({extent / 2, extent / 2, extent, extent}));
    ui.draw(renderer, scene, CanvasCamera{}.frame({extent / 2, extent / 2, extent, extent}));
    auto greenPixels = [&](bool future = false)
    {
        int count = 0;
        for (int y = 0; y < 44; ++y)
            for (int x = future ? static_cast<int>(graphWidth / 2) + 2 : 0;
                 x < static_cast<int>(future ? graphWidth : graphWidth / 2) - 2;
                 ++x)
            {
                const auto color = colorAt(graphX + x, list.y + y);
                if (color[1] > color[0] + 20 && color[1] > color[2] + 20)
                    ++count;
            }
        return count;
    };
    require(
        ui.clockWaveformTooDense(1000) && greenPixels() == 0 && greenPixels(true) == 0,
        "An unreadable clock still drew dense lines or a shaded block."
    );
    saveImage("clock-panel-too-fast.ppm");
    const auto slowRate = EditorActions(scene).apply(
        {ConfigureComponentProperties{.componentId = fast, .clockFrequency = 0.1f}}
    );
    require(
        static_cast<bool>(slowRate) && !ui.clockWaveformTooDense(0.1f),
        "Historical-density fixture still exceeded the nominal rate limit."
    );
    renderer.beginFrame(CanvasCamera{}.frame({extent / 2, extent / 2, extent, extent}));
    ui.draw(renderer, scene, CanvasCamera{}.frame({extent / 2, extent / 2, extent, extent}));
    require(
        greenPixels() == 0 && greenPixels(true) > 0,
        "Dense history was drawn or suppressed the independently readable clock preview."
    );
    const auto restoreRate = EditorActions(scene).apply(
        {ConfigureComponentProperties{.componentId = fast, .clockFrequency = 1000.0f}}
    );
    require(static_cast<bool>(restoreRate), "Could not restore the fitted clock fixture.");
    clickControl(UI::ClockControl::Fit);
    require(ui.clockWindowSeconds() == 0.004, "High-frequency waveform did not fit four cycles.");
    UiInputEvent hover{UiInputKind::Cursor};
    const auto fit = ui.clockControlBounds(UI::ClockControl::Fit);
    hover.x = fit.x + 2;
    hover.y = fit.y + 2;
    ui.handleInput(
        hover, scene, input, CanvasCamera{}.frame({extent / 2, extent / 2, extent, extent})
    );
    ui.update(20, input);
    ui.update(20.8, input);
    renderer.beginFrame(CanvasCamera{}.frame({extent / 2, extent / 2, extent, extent}));
    ui.draw(renderer, scene, CanvasCamera{}.frame({extent / 2, extent / 2, extent, extent}));
    require(
        !ui.clockWaveformTooDense(1000) && greenPixels() > 40,
        "Fit did not restore visible waveform edges."
    );
    const auto popup = ui.clockTooltipBounds(scene);
    require(
        popup.width > 0 && colorAt(popup.x + 4, popup.y + 4)[2] > 30,
        "Delayed clock-control tooltip did not render at 2x scaling."
    );
    require(
        views.activeClockTimeline().traces().at({fast}).size() >= 2000,
        "Suppressing/fitting waveforms discarded simulated transitions."
    );
    saveImage("clock-panel-fitted-tooltip.ppm");
    views.create(input);
    auto& multiple = views.activeScene();
    multiple.addComponent(BuiltinComponentIds::Clock, {0, 0});
    multiple.addComponent(
        BuiltinComponentIds::Clock, {20, 0}, ComponentOverrides{.clockFrequency = 2.0f}
    );
    multiple.stepAllClocks(1, &views.activeClockTimeline());
    multiple.stepAllClocks(2, &views.activeClockTimeline());
    ui.layout(multiple.getComponentCatalog(), {extent / 2, extent / 2, extent, extent}, input);
    const auto multipleFrame = CanvasCamera{}.frame({extent / 2, extent / 2, extent, extent});
    renderer.beginFrame(multipleFrame);
    ui.draw(renderer, multiple, multipleFrame);
    const double fastOnlyEdge =
        graphX + (0.25 - ui.clockStartTime()) / ui.clockWindowSeconds() * graphWidth;
    require(
        colorAt(fastOnlyEdge, list.y + 5)[2] < 65 && colorAt(fastOnlyEdge, list.y + 53)[2] > 65,
        "One clock's edge guide crossed into another clock's row."
    );
    saveImage("clock-panel-multiple.ppm");
    ui.setCircuitViews(nullptr, nullptr);
    input.setScene(nullptr);
}

void signalPresentation(Renderer& renderer)
{
    for (int dpi : {1, 2})
    {
        CircuitViews views;
        Input input;
        views.select(0, input);
        auto& scene = views.mainScene();
        const int data = scene.addComponent(BuiltinComponentIds::Input, {-12, 2});
        const int clock = scene.addComponent(BuiltinComponentIds::Clock, {-12, -2});
        const int flip = scene.addComponent(BuiltinComponentIds::DFlipFlop, {0, 0});
        require(
            static_cast<bool>(EditorActions(scene).apply(
                {ConfigureComponentProperties{.componentId = data, .label = "Data"},
                 ConfigureComponentProperties{.componentId = flip, .label = "Register"},
                 AddWire{{{-11, 2}, {-3, 2}, {-3, 1}}},
                 AddWire{{{-11, -2}, {-3, -2}, {-3, -1}}}}
            )),
            "Signal render fixture failed."
        );
        scene.handleClick(data);
        scene.propagate();
        auto& timeline = views.activeClockTimeline();
        scene.prepareSignalTraces(timeline);
        for (auto [id, index] : {std::pair{clock, 0u}, std::pair{flip, 0u}, std::pair{flip, 1u}})
        {
            const auto* view = scene.getCommittedComponentView(id);
            const auto& pin = view->getOutputPins()[index];
            timeline.toggleSignal(
                {{id},
                 PinType::OUTPUT,
                 index,
                 pin.id,
                 scene.signalName(id, PinType::OUTPUT, index),
                 {}}
            );
        }
        scene.stepAllClocks(2, &timeline);
        scene.stepAllClocks(1, &timeline);
        UI ui;
        ui.setCircuitViews(&views, &renderer.fontMetrics());
        const CanvasSurface surface{extent / dpi, extent / dpi, extent, extent};
        auto layout = [&] { ui.layout(scene.getComponentCatalog(), surface, input); };
        layout();
        auto cameraFrame = [&]
        {
            CanvasCamera camera;
            camera.setViewport(input.getCanvasViewport());
            camera.setCenter(input.getPanOffset());
            camera.setZoom(input.getZoom());
            return camera.frame(surface);
        };
        auto screenPixel = [&](double x, double y)
        {
            std::array<unsigned char, 4> pixel;
            glReadPixels(
                static_cast<int>(x * dpi),
                extent - 1 - static_cast<int>(y * dpi),
                1,
                1,
                GL_RGBA,
                GL_UNSIGNED_BYTE,
                pixel.data()
            );
            return pixel;
        };
        auto event = [&](UiInputEvent e) { return ui.handleInput(e, scene, input, cameraFrame()); };
        auto click = [&](CanvasViewport b, int button = GLFW_MOUSE_BUTTON_LEFT)
        {
            UiInputEvent e{UiInputKind::MouseButton};
            e.code = button;
            e.action = GLFW_PRESS;
            e.x = b.x + b.width / 2;
            e.y = b.y + b.height / 2;
            event(e);
            e.action = GLFW_RELEASE;
            event(e);
        };
        // Give all three channels room using the existing panel grip.
        const auto grip = ui.bottomResizeBounds();
        UiInputEvent resize{UiInputKind::MouseButton};
        resize.code = GLFW_MOUSE_BUTTON_LEFT;
        resize.action = GLFW_PRESS;
        resize.x = grip.x + 8;
        resize.y = grip.y + 2;
        event(resize);
        UiInputEvent drag{UiInputKind::Cursor};
        drag.x = resize.x;
        drag.y = surface.windowHeight - 330;
        event(drag);
        resize.action = GLFW_RELEASE;
        resize.y = drag.y;
        event(resize);
        layout();
        click(ui.bottomTabBounds(UI::BottomTab::Signals));
        auto draw = [&]
        {
            const auto camera = cameraFrame();
            renderer.beginFrame(camera);
            const auto components = buildComponentPresentation(scene.getComponentViewMap());
            const auto junctions = scene.getWireIntersections();
            renderer.drawCanvas(CanvasFrame{components, scene.getVisibleWires(), junctions});
            ui.draw(renderer, scene, camera);
        };
        draw();
        saveImage(dpi == 1 ? "signal-traces.ppm" : "signal-traces-2x.ppm");
        const auto list = ui.clockListBounds();
        const double labelWidth =
            std::min(220.0, std::min(list.width * 0.65, std::max(160.0, list.width * 0.4)));
        const double graphX = list.x + labelWidth;
        int green = 0;
        for (double x = graphX + 1; x < list.x + list.width; x += 2)
        {
            const auto pixel = screenPixel(x, list.y + 9);
            green += pixel[1] > 120 && pixel[1] > pixel[0] * 1.5;
        }
        require(green > 4, "Signal waveform is missing from the panel.");
        const double nowFraction =
            (timeline.time() - ui.clockStartTime()) / ui.clockWindowSeconds();
        require(
            nowFraction > 0 && nowFraction <= 1 &&
                (timeline.time() < ui.clockWindowSeconds() || std::abs(nowFraction - 1) < 1e-9),
            "Signals no longer use their original history-only time frame."
        );
        int futureGreen = 0;
        const double nowX = graphX + nowFraction * (list.width - labelWidth);
        for (double x = nowX + 8; x < list.x + list.width; x += 2)
        {
            const auto pixel = screenPixel(x, list.y + 9);
            futureGreen += pixel[1] > 120 && pixel[1] > pixel[0] * 1.5;
        }
        require(futureGreen == 0, "Signals projected unrecorded values into the future.");
        const auto point =
            *cameraFrame().worldToWindow(scene.getCommittedComponentView(flip)->getPosition());
        click({point.x - 1, point.y - 1, 2, 2}, GLFW_MOUSE_BUTTON_RIGHT);
        draw();
        require(
            ui.signalTraceBounds(scene, PinType::OUTPUT, 0).width > 0,
            "Popup lost its pin trace control."
        );
        saveImage(dpi == 1 ? "signal-trace-popup.ppm" : "signal-trace-popup-2x.ppm");
        ui.dismissPopups(input);
        // Bounded dense-region handling must not create hundreds of overlapping edges.
        auto& dense = timeline.signals()[0].samples;
        dense.clear();
        for (int i = 0; i < 1024; ++i)
            dense.push_back({i / 1024.0, bool(i % 2), true});
        draw();
        int denseGreen = 0;
        for (double x = graphX + 1; x < list.x + list.width; x += 2)
        {
            const auto pixel = screenPixel(x, list.y + 9);
            denseGreen += pixel[1] > 120 && pixel[1] > pixel[0] * 1.5;
        }
        require(denseGreen == 0, "Dense signal waveform was drawn instead of suppressed.");
        saveImage(dpi == 1 ? "signal-traces-dense.ppm" : "signal-traces-dense-2x.ppm");

        ui.setCircuitViews(nullptr, nullptr);
        input.setScene(nullptr);
    }
}


} // namespace

int main(int argc, char** argv)
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
            if (argc > 1 && std::string(argv[1]) == "--profile")
            {
                drawingProfile(renderer, camera);
                debugOverlayCacheChecks(renderer);
            }
            else if (argc > 1 && std::string(argv[1]) == "--clocks")
            {
                clockPresentation(renderer);
                signalPresentation(renderer);
            }
            else if (argc > 1 && std::string(argv[1]) == "--instances")
                mixedInstances(renderer, camera);
            else if (argc > 1 && std::string(argv[1]) == "--examples")
                examplesPresentation(renderer);
            else
            {
                renderResourceChecks();
                drawingProfile(renderer, camera);
                mixedInstances(renderer, camera);
                performanceOverlay(renderer);
                debugOverlayCacheChecks(renderer);
                nativeContacts(renderer, camera);
                editedGate(renderer, camera);
                inputRailPresentation(renderer, camera);
                inversionProportions(renderer, camera);
                inputInversionPresentation(renderer, camera);
                orientationPresentation(renderer, camera);
                generalizedOrientationPresentation(renderer, camera);
                preview(renderer, camera);
                pastePreview(renderer, camera);
                canvasViewport(renderer);
                wireMarkers(renderer, camera);
                roundedBounds(renderer, camera);
                componentOutlines(renderer);
                palettePresentation(renderer);
                outputPresentation(renderer);
                componentInformationPresentation(renderer);
                flipFlopPresentation(renderer);
                truthTablePresentation(renderer);
                customTruthTablePresentation(renderer);
                liveSubcircuitPresentation(renderer);
                gateSettingsPresentation(renderer);
                navigationMenuPresentation(renderer);
                examplesPresentation(renderer);
                labelVisibilityPresentation(renderer, camera);
                groupSelectionPresentation(renderer, camera);
                routedWiresPresentation(renderer, camera);
                circuitViewsPresentation(renderer);
                bottomResizePresentation(renderer);
                clockPresentation(renderer);
                signalPresentation(renderer);
            }
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
