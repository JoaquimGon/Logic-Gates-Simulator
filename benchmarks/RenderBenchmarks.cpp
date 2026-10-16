#include "Benchmark.h"
#include "Components/Definitions/NativeDefinitions.h"
#include "Geometry/GridSystem.h"
#include "Graphics/Renderer.h"
#include "Graphics/WireGeometry.h"
#include "Validation.h"

#include <GLFW/glfw3.h>
#include <algorithm>
#include <array>
#include <cmath>

namespace bench
{
namespace
{
struct Graphics
{
    GLFWwindow* window = nullptr;
    GLuint framebuffer = 0, texture = 0;
    Renderer renderer;
    bool loaded = false;


    /** @brief Release renderer resources while the benchmark's graphics context is still current. */
    ~Graphics()
    {
        renderer.shutdown();
        if (loaded)
        {
            glDeleteFramebuffers(1, &framebuffer);
            glDeleteTextures(1, &texture);
        }
        if (window)
            glfwDestroyWindow(window);
        glfwTerminate();
    }
};


std::vector<Pixel> image()
{
    std::vector<Pixel> pixels(1920 * 1080);
    glReadPixels(0, 0, 1920, 1080, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    return pixels;
}


struct PixelProbe
{
    int index;
    Pixel expected;
    std::string kind;
};


int pixelIndex(const CanvasCameraFrame& frame, glm::vec2 world)
{
    const auto screen = *frame.worldToWindow(world);
    const int x = int(screen.x), y = 1079 - int(screen.y);
    check(x >= 0 && x < 1920 && y >= 0 && y < 1080, "Validation pixel lies outside framebuffer.");
    return y * 1920 + x;
}


/** @brief Choose raster-covered interior sample pixels and derive expected component, lead and wire colours. */
std::vector<PixelProbe> probes(
    const CanvasCameraFrame& frame, const std::vector<ComponentRenderData>& data, const std::map<WireId, Wire>& wires,
    const std::vector<Pixel>& background
)
{
    std::vector<PixelProbe> result;
    for (const auto& component : data)
        for (const auto offset : std::array{glm::vec2{0, 0}, glm::vec2{.08f, .08f}, glm::vec2{.08f, -.08f}})
        {
            const int index = pixelIndex(frame, component.body.position + offset * component.body.size);
            const auto& tint = component.body.style.tint;
            Pixel expected{0, 0, 0, 255};
            // Interior samples avoid SDF edges/rims. RGB8 blending is alpha*tint+(1-alpha)*grid.
            for (int c = 0; c < 3; ++c)
                expected[c] = static_cast<unsigned char>(
                    std::lround(255 * tint[c] * tint[3] + background[index][c] * (1 - tint[3]))
                );
            result.push_back({index, expected, "component"});
        }
    for (const auto& [id, wire] : wires)
    {
        const auto& path = wire.getPath();
        const auto a = GridSystem::gridToWorld(path.front()), b = GridSystem::gridToWorld(path.back());
        for (const float t : {.45f, .7f})
        {
            const auto world = a + (b - a) * t;
            const int index = pixelIndex(frame, world);
            const int x = index % 1920, y = index / 1920;
            const auto center = *frame.windowToWorld({x + .5, 1079 - y + .5});
            // At large N some wires are subpixel. Only expect fragments whose pixel
            // centers lie strictly inside the actual rectangle, away from its raster edge.
            if (std::abs(center.y - world.y) > WireHalfWidth * .8f)
                continue;
            const Pixel expected = wire.getState() == PinState::ON    ? Pixel{0, 255, 0, 255}
                                   : wire.getState() == PinState::OFF ? Pixel{255, 0, 0, 255}
                                                                      : Pixel{0, 0, 255, 255};
            result.push_back({index, expected, "wire"});
        }
    }
    check(
        std::count_if(result.begin(), result.end(), [](const auto& p) { return p.kind == "wire"; }) >= 2,
        "Fixture has insufficient rasterized wire probes at this zoom."
    );
    return result;
}


bool matches(const std::vector<Pixel>& pixels, const std::vector<PixelProbe>& expected, const std::string& kind = "")
{
    for (const auto& probe : expected)
        if ((kind.empty() || probe.kind == kind) && !pixelMatches(pixels[probe.index], probe.expected))
            return false;
    return true;
}


/** @brief Compare actual framebuffer pixels against independently derived expected RGB values. */
void validate(const std::vector<Pixel>& pixels, const std::vector<PixelProbe>& expected)
{
    for (const auto& probe : expected)
        if (!pixelMatches(pixels[probe.index], probe.expected))
        {
            const auto a = pixels[probe.index], b = probe.expected;
            throw std::runtime_error(
                "Wrong " + probe.kind + " pixel at " + std::to_string(probe.index) + ": actual " +
                std::to_string(a[0]) + "," + std::to_string(a[1]) + "," + std::to_string(a[2]) + "; expected " +
                std::to_string(b[0]) + "," + std::to_string(b[1]) + "," + std::to_string(b[2])
            );
        }
}


/** @brief Check fixed-scale native pin colours and require deliberately invalid frames to fail the pixel validator. */
void validatePins(Renderer& renderer)
{
    // Keep pin cores well above one pixel; fitted benchmark scenes can shrink
    // points below a pixel, where their shader deliberately blends rim coverage.
    CanvasCamera camera;
    camera.setZoom(2);
    const auto frame = camera.frame({1920, 1080, 1920, 1080});
    const auto definitions = nativeDefinitions();
    std::vector<ComponentRenderData> components;
    for (const auto& definition : definitions)
        if (definition.identity.id == BuiltinComponentIds::And || definition.identity.id == BuiltinComponentIds::Or ||
            definition.identity.id == BuiltinComponentIds::Xor)
        {
            const glm::vec2 position{(int(components.size()) - 1) * .5f, 0};
            ComponentRenderData component{
                {int(components.size()),
                 position,
                 {definition.layout.width, definition.layout.height},
                 definition.presentation.shader.key,
                 definition.presentation.body},
                "",
                false,
                {}
            };
            for (const auto& pin : definition.layout.pins)
            {
                const auto anchor = position + GridSystem::gridToWorld(pin.anchor);
                component.pins.push_back(
                    {pin.direction, pin.index, PinState::OFF, anchor, "", buildPinLead(component.body, anchor)}
                );
            }
            components.push_back(std::move(component));
        }
    check(components.size() == 3, "Missing pin-validation component types.");
    std::vector<PixelProbe> off;
    for (const auto state : {PinState::OFF, PinState::ON, PinState::DISCONNECTED})
    {
        std::vector<PixelProbe> expected;
        const Pixel color = state == PinState::OFF  ? Pixel{255, 0, 0, 255}
                            : state == PinState::ON ? Pixel{0, 255, 0, 255}
                                                    : Pixel{0, 0, 255, 255};
        for (auto& component : components)
            for (auto& pin : component.pins)
            {
                pin.state = state;
                expected.push_back({pixelIndex(frame, pin.position), color, "pin"});
            }
        renderer.beginFrame(frame);
        renderer.drawCanvas({components, {}, {}});
        const auto pixels = image();
        validate(pixels, expected);
        if (state == PinState::OFF)
            off = expected;
        else
            check(!matches(pixels, off), "Pin validator accepted the wrong state.");
    }
}
} // namespace


void rendering(const Config& config, Recorder& results)
{
    Graphics graphics;
    if (!glfwInit())
    {
        check(!config.requireRendering, "Required rendering validation cannot initialize GLFW.");
        results.outcome("rendering", 0, "unavailable", "GLFW initialization failed.");
        return;
    }
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    graphics.window = glfwCreateWindow(64, 64, "Release rendering benchmark", nullptr, nullptr);
    if (!graphics.window)
    {
        check(!config.requireRendering, "Required rendering validation cannot create OpenGL context.");
        results.outcome("rendering", 0, "unavailable", "Cannot create an OpenGL 3.3 context.");
        return;
    }
    glfwMakeContextCurrent(graphics.window);
    check(gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress)), "Cannot load OpenGL.");
    graphics.loaded = true;
    results.metadata["gpu"] = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    results.metadata["gpu_vendor"] = reinterpret_cast<const char*>(glGetString(GL_VENDOR));
    results.metadata["opengl"] = reinterpret_cast<const char*>(glGetString(GL_VERSION));
    results.metadata["render_resolution"] = {1920, 1080};
    results.metadata["render_notes"] =
        "Offscreen renderer only: grid, N bodies, 3N pins/leads and N wire routes, all visible. No "
        "application UI, OS events, swap/VSync or simulation. GPU queries are retrieved after "
        "batch completion, outside CPU submission samples.";
    results.metadata["render_zoom_policy"] =
        "Fixed 1920x1080 resolution, fitted zoom: visible symbol size decreases as N increases.";
    results.metadata["render_pixel_validation"] =
        "Three interior RGB probes per body; two interior raster-covered probes per eligible wire. "
        "Expected tint/alpha over separately rendered grid, exact red/green/blue wire colors; "
        "tolerance 4/255 covers RGB8 rounding/blending and driver arithmetic, not edge "
        "anti-aliasing. Background, missing body/wire and wrong state controls. Reads/checks "
        "outside timings.";
    results.metadata["render_controlled_pair"] = {
        "render_mixed_static vs render_mixed_grouped_static", "render_mixed_active vs render_mixed_grouped_active"
    };
    glGenTextures(1, &graphics.texture);
    glBindTexture(GL_TEXTURE_2D, graphics.texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1920, 1080, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glGenFramebuffers(1, &graphics.framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, graphics.framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, graphics.texture, 0);
    check(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE, "Benchmark framebuffer incomplete.");
    check(graphics.renderer.init(), "Renderer initialization failed.");
    validatePins(graphics.renderer);
    results.outcome(
        "render_pin_state_controls", 3, "pass",
        "Nine native gate pin cores checked against exact OFF/ON/disconnected colors at fixed "
        "zoom; wrong states rejected. Untimed."
    );
    GLint timerBits = 0;
    glGetQueryiv(GL_TIME_ELAPSED, GL_QUERY_COUNTER_BITS, &timerBits);
    results.metadata["gpu_timer_bits"] = timerBits;
    std::vector<GLuint> queries(config.samples);
    if (timerBits)
        glGenQueries(config.samples, queries.data());
    for (const int size : config.sizes)
        for (const int ordering : {0, 1, 2})
            for (const bool active : {false, true})
            {
                const bool mixed = ordering != 0;
                const std::string scenario = std::string("render_") +
                                             (ordering == 2 ? "mixed_grouped"
                                              : mixed       ? "mixed"
                                                            : "same") +
                                             (active ? "_active" : "_static");
                const int columns = int(std::ceil(std::sqrt(size * 1920.0 / 1080.0)));
                const int rows = (size + columns - 1) / columns;
                const auto definitions = nativeDefinitions();
                std::array<const ComponentDefinition*, 3> types{};
                for (const auto& definition : definitions)
                    for (int i = 0; i < 3; ++i)
                        if (definition.identity.id ==
                            std::array{BuiltinComponentIds::And, BuiltinComponentIds::Or, BuiltinComponentIds::Xor}[i])
                            types[i] = &definition;
                std::vector<ComponentRenderData> data;
                std::map<WireId, Wire> wires;
                for (int i = 0; i < size; ++i)
                {
                    const auto& definition = *types[mixed ? i % 3 : 0];
                    const GridCoords origin{(i % columns) * 8, (i / columns) * 8};
                    const auto position = GridSystem::gridToWorld(origin);
                    ComponentRenderData component{
                        {ordering == 2 ? (i % 3) * size + i : i,
                         position,
                         {definition.layout.width, definition.layout.height},
                         definition.presentation.shader.key,
                         definition.presentation.body},
                        "",
                        false,
                        {}
                    };
                    for (const auto& pin : definition.layout.pins)
                    {
                        const auto anchor = position + GridSystem::gridToWorld(pin.anchor);
                        component.pins.push_back(
                            {pin.direction, pin.index, PinState::OFF, anchor, "", buildPinLead(component.body, anchor)}
                        );
                    }
                    data.push_back(std::move(component));
                    Wire wire;
                    wire.setPath({{origin.x + 2, origin.y}, {origin.x + 5, origin.y}});
                    wire.setState(PinState::OFF);
                    wires.emplace(i, std::move(wire));
                }
                CanvasCamera camera;
                camera.setCenter(GridSystem::gridToWorld({(columns - 1) * 4, (rows - 1) * 4}));
                const double spacing = GridSystem::GRID_SPACING;
                camera.setZoom(
                    float(std::min(2.0 * 1920 / 1080 / (columns * 8 * spacing), 2.0 / (rows * 8 * spacing)) / 1.05)
                );
                const auto frame = camera.frame({1920, 1080, 1920, 1080});
                auto mutate = [&](int sample)
                {
                    if (!active)
                        return;
                    for (auto& component : data)
                    {
                        component.body.style.tint[3] = sample % 2 ? 0.85f : 1.0f;
                        for (auto& pin : component.pins)
                            pin.state = sample % 2 ? PinState::ON : PinState::OFF;
                    }
                    for (auto& [id, wire] : wires)
                        wire.setState(sample % 2 ? PinState::ON : PinState::OFF);
                };
                auto draw = [&]
                {
                    graphics.renderer.beginFrame(frame);
                    graphics.renderer.drawCanvas({data, wires, {}});
                };
                graphics.renderer.beginFrame(frame);
                graphics.renderer.drawCanvas({{}, {}, {}});
                const auto background = image();
                auto expected = probes(frame, data, wires, background);
                check(!matches(background, expected, "component"), "Component validator accepted a grid-only frame.");
                // Actual GL negative controls, not only synthetic byte-array tests.
                graphics.renderer.beginFrame(frame);
                graphics.renderer.drawCanvas({{}, wires, {}});
                check(!matches(image(), expected, "component"), "Component validator accepted missing bodies.");
                graphics.renderer.beginFrame(frame);
                graphics.renderer.drawCanvas({data, {}, {}});
                check(!matches(image(), expected, "wire"), "Wire validator accepted missing wires.");
                for (const PinState state : {PinState::OFF, PinState::ON, PinState::DISCONNECTED})
                {
                    for (auto& component : data)
                        for (auto& pin : component.pins)
                            pin.state = state;
                    for (auto& [id, wire] : wires)
                        wire.setState(state);
                    draw();
                    const auto pixels = image();
                    const auto stateExpected = probes(frame, data, wires, background);
                    validate(pixels, stateExpected);
                    if (state == PinState::ON)
                        check(!matches(pixels, expected, "wire"), "Wire validator accepted the wrong logic state.");
                }
                for (auto& component : data)
                    for (auto& pin : component.pins)
                        pin.state = PinState::OFF;
                for (auto& [id, wire] : wires)
                    wire.setState(PinState::OFF);
                if (active)
                    for (int state : {0, 1})
                    {
                        mutate(state);
                        draw();
                        validate(image(), probes(frame, data, wires, background));
                    }
                for (int trial = 0; trial < config.trials; ++trial)
                {
                    for (int warmup = 0; warmup < config.warmup; ++warmup)
                    {
                        mutate(warmup);
                        draw();
                    }
                    glFinish();
                    std::vector<double> cpu;
                    const auto batch = Clock::now();
                    for (int sample = 0; sample < config.samples; ++sample)
                    {
                        mutate(sample);
                        if (timerBits)
                            glBeginQuery(GL_TIME_ELAPSED, queries[sample]);
                        const auto start = Clock::now();
                        draw();
                        cpu.push_back(milliseconds(start));
                        if (timerBits)
                            glEndQuery(GL_TIME_ELAPSED);
                    }
                    glFinish(); // Drain once per batch; never force completion between measured
                                // frames.
                    const double completed = milliseconds(batch);
                    const auto drawCalls = graphics.renderer.getDrawCallCount();
                    check(glGetError() == GL_NO_ERROR, "OpenGL error in rendering benchmark.");
                    const auto pixels = image();
                    validate(pixels, probes(frame, data, wires, background));
                    if (ordering == 2)
                    {
                        for (int i = 0; i < size; ++i)
                            data[i].body.id = i;
                        draw();
                        check(image() == pixels, "Batch ordering changed the controlled fixture's visual composition.");
                        for (int i = 0; i < size; ++i)
                            data[i].body.id = (i % 3) * size + i;
                    }
                    for (int sample = 0; sample < config.samples; ++sample)
                    {
                        results.sample(scenario, size, trial, sample, "cpu_submit_ms", cpu[sample], "ms");
                        if (timerBits)
                        {
                            GLuint64 nanoseconds = 0;
                            glGetQueryObjectui64v(queries[sample], GL_QUERY_RESULT, &nanoseconds);
                            results.sample(
                                scenario, size, trial, sample, "gpu_ms", double(nanoseconds) / 1000000, "ms"
                            );
                        }
                    }
                    results.sample(
                        scenario, size, trial, 0, "completed_batch_frame_ms", completed / config.samples, "ms"
                    );
                    results.sample(scenario, size, trial, 0, "draw_calls", drawCalls, "count");
                }
                results.outcome(
                    scenario, size, "pass",
                    ordering == 2 ? "Same mixed composition/positions/shaders as interleaved case; "
                                    "only body IDs determining batch order change. Full "
                                    "framebuffer equivalence checked."
                    : mixed       ? "Interleaved shaders; paired with mixed_grouped, identical visual "
                                    "composition."
                                  : "Identical shader, favorable batching."
                );
            }
    if (timerBits)
        glDeleteQueries(config.samples, queries.data());
}
} // namespace bench
