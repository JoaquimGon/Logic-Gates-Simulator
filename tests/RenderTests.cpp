#include "Components/Definitions/NativeDefinitions.h"
#include "Editor/Actions/EditorActions.h"
#include "Editor/Scene.h"
#include "Graphics/Presentation/ComponentPresentation.h"
#include "Graphics/Renderer.h"

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

void mixedInstances(Renderer& renderer, const CameraState& camera)
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

void nativeContacts(Renderer& renderer, const CameraState& camera)
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

void preview(Renderer& renderer, const CameraState& camera)
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
        {RegisterComponentDefinition{custom}, CreateComponent{custom.identity.id, {13, -7}}}
    );
    require(static_cast<bool>(added), "Custom visual preview could not be created.");
    const auto components = buildComponentPresentation(scene.getComponentViewMap());
    const auto junctions = scene.getWireIntersections();
    CanvasFrame frame{components, scene.getWires(), junctions};
    frame.bodyHighlight =
        BodyHighlight{components.back().body.position, components.back().body.size, 1};
    renderer.beginFrame(camera);
    renderer.drawCanvas(frame);
    DebugMetrics metrics{};
    metrics.evalResult = EvalOrderResult::CONNECTION_REJECTED;
    renderer.drawDebugOverlay(metrics, false);
    require(glGetError() == GL_NO_ERROR, "Canvas/text pass produced an OpenGL error.");
    saveImage("component-presentations.ppm");
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
            const CameraState camera{{0, 0}, 1, 1, extent, extent};
            mixedInstances(renderer, camera);
            nativeContacts(renderer, camera);
            preview(renderer, camera);
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
