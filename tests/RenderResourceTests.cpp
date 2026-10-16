#include "Graphics/Renderer.h"

#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <thread>

namespace
{
void require(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}


struct DrawingProbe
{
    static inline std::size_t allocations = 0, uploads = 0, bytes = 0, lookups = 0;
    static inline PFNGLBUFFERDATAPROC bufferData = nullptr;
    static inline PFNGLBUFFERSUBDATAPROC bufferSubData = nullptr;
    static inline PFNGLGETUNIFORMLOCATIONPROC getUniform = nullptr;


    /** @brief Create an offscreen framebuffer attachment and reject incomplete rendering setup. */
    static void APIENTRY allocate(GLenum target, GLsizeiptr size, const void* data, GLenum usage)
    {
        ++allocations;
        if (data)
        {
            ++uploads;
            bytes += size;
        }
        bufferData(target, size, data, usage);
    }


    static void APIENTRY upload(GLenum target, GLintptr offset, GLsizeiptr size, const void* data)
    {
        ++uploads;
        bytes += size;
        bufferSubData(target, offset, size, data);
    }


    static GLint APIENTRY lookup(GLuint program, const GLchar* name)
    {
        ++lookups;
        return getUniform(program, name);
    }


    /** @brief Initialize a controlled drawing target and capture its GL resource state for reuse checks. */
    DrawingProbe()
    {
        allocations = uploads = bytes = lookups = 0;
        bufferData = glad_glBufferData;
        bufferSubData = glad_glBufferSubData;
        getUniform = glad_glGetUniformLocation;
        glad_glBufferData = allocate;
        glad_glBufferSubData = upload;
        glad_glGetUniformLocation = lookup;
    }


    ~DrawingProbe()
    {
        glad_glBufferData = bufferData;
        glad_glBufferSubData = bufferSubData;
        glad_glGetUniformLocation = getUniform;
    }
};

} // namespace


/** @brief Verify draw categories and CPU accounting match actual canvas and UI submissions. */
void drawingProfile(Renderer& renderer, const CanvasCameraFrame& camera)
{
    std::vector<ComponentRenderData> components;
    std::map<WireId, Wire> wires;
    for (int i = 0; i < 160; ++i)
    {
        const glm::vec2 center{-0.85f + (i % 16) * 0.11f, -0.8f + (i / 16) * 0.16f};
        ComponentRenderData component{{i, center, {0.07f, 0.09f}, "ANDgate", {}}, "", false, {}};
        for (int pin = 0; pin < 4; ++pin)
        {
            const auto point = center + glm::vec2{-0.055f, (pin - 1.5f) * 0.02f};
            component.pins.push_back(
                {PinType::INPUT,
                 static_cast<unsigned>(pin),
                 PinState::OFF,
                 point,
                 "",
                 {point + glm::vec2{0.02f, 0}, point}}
            );
        }
        components.push_back(std::move(component));
        Wire wire;
        wire.setPath({{i % 16, i / 16}, {i % 16 + 1, i / 16}});
        wires.emplace(i, std::move(wire));
    }
    auto draw = [&]
    {
        renderer.beginFrame(camera);
        renderer.drawPinLeads(components);
        renderer.drawComponents(components);
        renderer.drawWires(wires);
        renderer.drawPins(components);
    };
    draw(); // Warm storage, layouts and uniforms before measuring.
    for (bool edited : {false, true})
    {
        DrawingProbe probe;
        const auto start = std::chrono::steady_clock::now();
        for (int frame = 0; frame < 80; ++frame)
        {
            if (edited)
            {
                components.front().body.size.y = frame % 2 ? 0.09f : 0.12f;
                components.front().pins.front().state = frame % 2 ? PinState::ON : PinState::OFF;
                components.front().pins.front().lead.front().y = frame % 2 ? -0.81f : -0.83f;
                wires.begin()->second.setState(frame % 2 ? PinState::ON : PinState::OFF);
            }
            draw();
        }
        const auto ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        require(probe.allocations == 0, "Warm buffers reallocated storage without growth.");
        require(probe.lookups == 0, "Repeated drawing bypassed the uniform cache.");
        require(
            edited ? probe.uploads > 0 : probe.uploads == 0,
            "Unchanged buffers uploaded, or edited buffers failed to upload."
        );
        std::cout << "PROFILE " << (edited ? "edited" : "static") << ": " << ms
                  << " ms/80 frames, storage=" << probe.allocations << ", uploads=" << probe.uploads
                  << ", bytes=" << probe.bytes << ", uniform lookups=" << probe.lookups << '\n';
    }
}


namespace
{
/** @brief Verify dynamic geometry reuses buffer allocations and refreshes active instance attributes. */
void bufferReuseChecks()
{
    VertexLayout layout;
    layout.addAttribute(3);
    Mesh mesh({}, {}, layout);
    const std::vector<float> small{0, 0, 0, 1, 0, 0, 0, 1, 0};
    mesh.updateData(small, 3);
    {
        DrawingProbe probe;
        mesh.updateData(small, 3);
        require(probe.allocations == 0 && probe.uploads == 0, "Identical vertex submission uploaded again.");
        auto moved = small;
        moved[0] = 0.25f;
        mesh.updateData(moved, 3);
        require(probe.allocations == 0 && probe.uploads == 1, "Movement did not update existing buffer storage.");
        auto large = moved;
        large.insert(large.end(), small.begin(), small.end());
        mesh.updateData(large, 3);
        require(probe.allocations == 1 && probe.uploads == 2, "Growing geometry did not allocate/upload new storage.");
        mesh.updateData(small, 3);
        require(
            probe.allocations == 1 && probe.uploads == 3,
            "Shrinking geometry replaced buffer storage or kept stale data."
        );
        mesh.updateData({}, 3);
        mesh.draw(); // Empty submissions must reset vertexCount.
        mesh.updateData(small, 3);
        require(
            probe.allocations == 1 && probe.uploads == 4,
            "Restored geometry after an empty submission retained stale data."
        );
    }
    mesh.setInstanceData({0, 0, 1, 0, 0, 1}, {2, 4}, 1);
    {
        DrawingProbe probe;
        mesh.setInstanceData({0, 0, 1, 0, 0, 1}, {2, 4}, 1);
        require(probe.allocations == 0 && probe.uploads == 0, "Identical instances uploaded again.");
        mesh.setInstanceData({0, 0, 0, 1, 0, 1}, {2, 4}, 1);
        require(probe.allocations == 0 && probe.uploads == 1, "Instance color change did not update existing storage.");
    }
}
} // namespace


/** @brief Verify resource teardown, shader reload failure recovery, include diagnostics and uniform-cache invalidation.
 */
void renderResourceChecks()
{
    bufferReuseChecks();
    const auto directory = std::filesystem::path(RENDER_ARTIFACTS_DIR) / "shader-reload";
    std::filesystem::create_directories(directory);
    const auto vertex = directory / "test.vert", fragment = directory / "test.frag";
    const auto helper = directory / "helper.glsl", inner = directory / "inner.glsl";

    struct Cleanup
    {
        std::vector<std::filesystem::path> files;

        ~Cleanup()
        {
            for (const auto& file : files)
            {
                std::error_code error;
                std::filesystem::remove(file, error);
            }
        }
    } cleanup{{vertex, fragment, helper, inner}};

    int revision = 0;
    const auto initialTime = std::filesystem::file_time_type::clock::now();
    auto write = [&](const std::filesystem::path& path, const std::string& source)
    {
        std::ofstream stream(path);
        stream << source;
        stream.close();
        std::filesystem::last_write_time(path, initialTime + std::chrono::seconds(++revision));
    };
    auto reload = [](Shader& shader)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(210));
        shader.checkAndReload();
    };
    write(vertex, "#version 330 core\nvoid main(){gl_Position=vec4(0,0,0,1);}\n");
    write(
        fragment, "#version 330 core\n#include \"helper.glsl\"\nout vec4 color;\nuniform float gain;\nvoid "
                  "main(){color=shade(gain);}\n"
    );
    write(helper, "#include \"inner.glsl\"\n");
    const std::string valid = "vec4 shade(float v){return vec4(v,0,0,1);}\n";
    write(inner, valid);
    Shader shader(vertex.string().c_str(), fragment.string().c_str());
    require(shader.isValid(), "Nested quoted shader includes did not compile.");
    shader.use();
    GLint original = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &original);
    {
        DrawingProbe probe;
        for (int i = 0; i < 4; ++i)
        {
            shader.setFloat("gain", 0.25f);
            shader.setFloat("absent", 0.5f);
        }
        require(probe.lookups == 2, "Present and missing uniforms were not cached.");
    }

    // Diagnostics must name the included source and appear only once per attempted revision.
    struct Capture
    {
        std::ostringstream errors;
        std::streambuf* previous = std::cerr.rdbuf(errors.rdbuf());

        ~Capture() { std::cerr.rdbuf(previous); }
    } capture;

    write(inner, "invalid GLSL;\n");
    reload(shader);
    const auto failure = capture.errors.str();
    require(
        failure.find(inner.lexically_normal().string()) != std::string::npos &&
            failure.find("source 2:") != std::string::npos,
        "Compiler diagnostics lost the nested include source mapping."
    );
    reload(shader);
    require(capture.errors.str() == failure, "Unchanged failed shader was compiled/logged again.");
    shader.use();
    GLint retained = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &retained);
    require(retained == original, "Failed reload replaced the working program.");
    {
        DrawingProbe probe;
        shader.setFloat("gain", 0.75f);
        shader.setFloat("absent", 0.5f);
        require(probe.lookups == 0, "Failed reload discarded the working uniform cache.");
    }
    write(inner, valid);
    // Timestamp comparisons must also notice a restored file with an older write time.
    std::filesystem::last_write_time(inner, initialTime - std::chrono::seconds(10));
    reload(shader);
    shader.use();
    GLint replacement = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &replacement);
    require(replacement != original, "Fixing an included helper did not replace the program.");
    {
        DrawingProbe probe;
        shader.setFloat("gain", 0.6f);
        shader.setFloat("gain", 0.6f);
        shader.setFloat("absent", 0.5f);
        require(probe.lookups == 2, "Successful reload reused the old program's uniform cache.");
    }
    GLfloat value = 0;
    glGetUniformfv(replacement, glGetUniformLocation(replacement, "gain"), &value);
    require(std::abs(value - 0.6f) < 0.0001f, "Cached uniform updated the wrong program/location.");
    std::filesystem::remove(inner);
    reload(shader);
    const auto missing = capture.errors.str();
    require(missing.find("Cannot read shader:") != std::string::npos, "Missing included file had no diagnostic.");
    reload(shader);
    require(capture.errors.str() == missing, "Missing dependency retried without an edit.");
    write(inner, valid);
    reload(shader);
    shader.use();
    GLint recovered = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &recovered);
    require(recovered != replacement, "Creating a missing dependency did not trigger recovery.");
    shader.setFloat("gain", 0.4f);
    write(
        vertex, "#version 330 core\nout vec3 mismatch;\nvoid "
                "main(){mismatch=vec3(1);gl_Position=vec4(0,0,0,1);}\n"
    );
    write(
        fragment, "#version 330 core\n#include \"helper.glsl\"\nin vec2 mismatch;\nout vec4 color;\nuniform "
                  "float gain;\nvoid main(){color=shade(gain+mismatch.x);}\n"
    );
    reload(shader);
    require(
        capture.errors.str().find("LINKING_FAILED") != std::string::npos,
        "Mismatched shader interfaces did not fail at link time."
    );
    shader.use();
    glGetIntegerv(GL_CURRENT_PROGRAM, &retained);
    require(retained == recovered, "Failed link replaced the valid program.");
    {
        DrawingProbe probe;
        shader.setFloat("gain", 0.3f);
        require(probe.lookups == 0, "Failed link discarded the working uniform cache.");
    }
    write(inner, "#include \"helper.glsl\"\n");
    reload(shader);
    require(
        capture.errors.str().find("Cyclic shader include:") != std::string::npos,
        "Cyclic includes did not report a bounded source error."
    );
    shader.use();
    glGetIntegerv(GL_CURRENT_PROGRAM, &retained);
    require(retained == recovered, "Cyclic include replaced the valid program.");
    require(glGetError() == GL_NO_ERROR, "Shader/buffer resource checks caused an OpenGL error.");
}


/** @brief Verify diagnostic text caching refreshes when data, timing context or electrical faults change. */
void debugOverlayCacheChecks(Renderer& renderer)
{
    using Clock = std::chrono::steady_clock;
    using namespace std::chrono_literals;
    constexpr int extent = 1024;
    CanvasSurface surface{extent, extent, extent, extent};
    DebugMetrics metrics;
    metrics.fps = 123.4f;
    metrics.timingsReady = true;
    const auto origin = Clock::time_point{} + 10s;
    auto begin = [&]()
    {
        renderer.beginFrame(CanvasCamera{}.frame(surface));
        // Other screen draws overwrite the ordinary UI buffers before every cached HUD draw.
        renderer.drawScreenRect({12, surface.windowHeight - 28.0, 110, 20}, {0.2f, 0.4f, 0.6f, 1});
        const std::vector<TextRun> text{{"Other UI text", {20, surface.windowHeight - 12.0}, 0.32f, {1, 1, 1, 1}}};
        renderer.drawText(text, TextSpace::Screen);
    };
    auto pixels = [&]()
    {
        std::vector<unsigned char> image(extent * extent * 4);
        glReadPixels(0, 0, extent, extent, GL_RGBA, GL_UNSIGNED_BYTE, image.data());
        return image;
    };
    renderer.invalidateDebugOverlay();
    begin();
    renderer.drawDebugOverlay(metrics, true, origin);
    const auto original = pixels();
    metrics.fps = 987.6f;
    begin();
    const auto draws = renderer.getDrawCallCount();
    {
        DrawingProbe probe;
        renderer.drawDebugOverlay(metrics, true, origin + 50ms);
        require(
            probe.uploads == 0 && probe.allocations == 0,
            "Cached HUD rebuilt/uploaded buffers after another UI submission."
        );
    }
    require(
        pixels() == original && renderer.getDrawCallCount() == draws,
        "HUD changed before its refresh interval or contaminated scene draw counts."
    );
    begin();
    {
        DrawingProbe probe;
        renderer.drawDebugOverlay(metrics, true, origin + 250ms);
        require(probe.uploads > 0, "HUD values did not refresh after 250 ms.");
    }
    const auto refreshed = pixels();
    require(refreshed != original, "HUD retained old values after its refresh deadline.");
    metrics.evalResult = SimulationResult::NON_CONVERGENT;
    begin();
    renderer.drawDebugOverlay(metrics, true, origin + 251ms);
    const auto failed = pixels();
    require(failed != refreshed, "Simulation failure waited for the numeric refresh deadline.");
    metrics.evalResult = SimulationResult::CONNECTION_REJECTED;
    begin();
    renderer.drawDebugOverlay(metrics, true, origin + 252ms);
    require(pixels() != failed, "Changed simulation error retained the previous warning.");
    metrics.evalResult = SimulationResult::OK;
    begin();
    renderer.drawDebugOverlay(metrics, true, origin + 253ms);
    require(pixels() == refreshed, "Repaired circuit retained cached error text.");
    metrics.shortedNetCount = 1;
    begin();
    renderer.drawDebugOverlay(metrics, true, origin + 254ms);
    require(pixels() != refreshed, "A new wiring short did not immediately refresh the HUD.");
    metrics.shortedNetCount = 0;
    begin();
    const auto hidden = pixels();
    renderer.drawDebugOverlay(metrics, false, origin + 255ms);
    require(pixels() == hidden, "Hidden healthy HUD left cached text/background visible.");
    metrics.fps = 80;
    begin();
    renderer.drawDebugOverlay(metrics, true, origin + 256ms);
    require(pixels() != refreshed, "Reopening the HUD retained the previous display sample.");
    surface = {extent / 2, extent / 2, extent, extent};
    begin();
    {
        DrawingProbe probe;
        renderer.drawDebugOverlay(metrics, true, origin + 257ms);
        require(probe.uploads > 0, "Window/DPI resize reused stale HUD positions.");
    }
    surface = {extent, extent, extent, extent};
    begin();
    renderer.drawDebugOverlay(metrics, true, origin + 258ms);
    metrics.fps = 90;
    renderer.invalidateDebugOverlay();
    begin();
    {
        DrawingProbe probe;
        renderer.drawDebugOverlay(metrics, true, origin + 259ms);
        require(probe.uploads > 0, "New profiling context retained stale cached values.");
    }
    renderer.shutdown();
    require(renderer.init(), "Renderer could not reinitialize its cached HUD/font resources.");
    begin();
    renderer.drawDebugOverlay(metrics, true, origin + 260ms);
    require(
        glGetError() == GL_NO_ERROR && glIsEnabled(GL_SCISSOR_TEST) == GL_FALSE,
        "Cached HUD lifecycle left invalid OpenGL or clipping state."
    );

    // Report timings without a machine-specific speed threshold; upload assertions verify the win.
    for (bool rebuild : {false, true})
    {
        const auto base = origin + (rebuild ? 3s : 2s);
        begin();
        renderer.drawDebugOverlay(metrics, true, base);
        std::size_t uploads = 0;
        const auto start = Clock::now();
        for (int frame = 1; frame <= 80; ++frame)
        {
            begin();
            metrics.fps = 90.0f + static_cast<float>(frame);
            if (rebuild)
                renderer.invalidateDebugOverlay();
            DrawingProbe probe;
            renderer.drawDebugOverlay(metrics, true, base + std::chrono::milliseconds(frame));
            uploads += probe.uploads;
        }
        const auto ms = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
        require(
            rebuild ? uploads > 0 : uploads == 0,
            "Warm cached HUD uploaded per frame or forced refresh failed to update."
        );
        std::cout << "PROFILE HUD " << (rebuild ? "forced-refresh" : "cached") << ": " << ms
                  << " ms/80 frames (with other UI), HUD uploads=" << uploads << '\n';
    }
}
