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
        const auto ms =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
                .count();
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
        require(
            probe.allocations == 0 && probe.uploads == 0,
            "Identical vertex submission uploaded again."
        );
        auto moved = small;
        moved[0] = 0.25f;
        mesh.updateData(moved, 3);
        require(
            probe.allocations == 0 && probe.uploads == 1,
            "Movement did not update existing buffer storage."
        );
        auto large = moved;
        large.insert(large.end(), small.begin(), small.end());
        mesh.updateData(large, 3);
        require(
            probe.allocations == 1 && probe.uploads == 2,
            "Growing geometry did not allocate/upload new storage."
        );
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
        require(
            probe.allocations == 0 && probe.uploads == 0, "Identical instances uploaded again."
        );
        mesh.setInstanceData({0, 0, 0, 1, 0, 1}, {2, 4}, 1);
        require(
            probe.allocations == 0 && probe.uploads == 1,
            "Instance color change did not update existing storage."
        );
    }
}
} // namespace

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
        fragment,
        "#version 330 core\n#include \"helper.glsl\"\nout vec4 color;\nuniform float gain;\nvoid "
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
    require(
        missing.find("Cannot read shader:") != std::string::npos,
        "Missing included file had no diagnostic."
    );
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
        vertex,
        "#version 330 core\nout vec3 mismatch;\nvoid "
        "main(){mismatch=vec3(1);gl_Position=vec4(0,0,0,1);}\n"
    );
    write(
        fragment,
        "#version 330 core\n#include \"helper.glsl\"\nin vec2 mismatch;\nout vec4 color;\nuniform "
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
