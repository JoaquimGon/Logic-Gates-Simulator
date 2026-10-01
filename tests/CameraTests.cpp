#include "Geometry/CanvasCamera.h"
#include "Geometry/GridMetrics.h"

#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace
{
void require(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}

void transforms()
{
    for (const auto surface :
         {CanvasSurface{800, 800, 800, 800},
          CanvasSurface{1200, 600, 1200, 600},
          CanvasSurface{600, 1000, 600, 1000},
          CanvasSurface{800, 600, 1200, 900},
          CanvasSurface{800, 600, 1600, 1200},
          CanvasSurface{800, 600, 1600, 1800}})
        for (const auto viewport :
             {std::optional<CanvasViewport>{},
              std::optional<CanvasViewport>{{120.25, 40.75, 401.3, 370.6}},
              std::optional<CanvasViewport>{{-40, -20, 380, 190}}})
            for (const auto center : {glm::vec2{0}, glm::vec2{0.35f, -0.2f}})
                for (const float zoom : {0.2f, 1.0f, 3.4f})
                {
                    CanvasCamera camera;
                    camera.setCenter(center);
                    camera.setZoom(zoom);
                    camera.setViewport(viewport);
                    const auto frame = camera.frame(surface);
                    require(frame.valid(), "Valid layout produced an inactive frame.");
                    const auto& bounds = frame.viewport;
                    const glm::dvec2 middle{
                        bounds.x + bounds.width / 2, bounds.y + bounds.height / 2
                    };
                    require(
                        glm::length(*frame.windowToWorld(middle) - center) < 0.00001f,
                        "Camera center does not map to canvas center."
                    );
                    const float aspect = static_cast<float>(frame.framebufferViewport.width) /
                                         frame.framebufferViewport.height;
                    const auto topLeft = frame.windowToWorld({bounds.x, bounds.y});
                    require(
                        glm::length(*topLeft - (center + glm::vec2{-aspect / zoom, 1 / zoom})) <
                            0.00001f,
                        "Top-left conversion disagrees with camera extent."
                    );
                    for (const auto point :
                         {center, center + glm::vec2{0.3f, -0.4f}, center + glm::vec2{-2, 3}})
                    {
                        const auto screen = frame.worldToWindow(point);
                        require(
                            screen && glm::length(*frame.windowToWorld(*screen) - point) < 0.00001f,
                            "Forward/inverse camera conversion disagrees."
                        );
                        const auto ndc = frame.viewProjection * glm::vec4{point, 0, 1};
                        const auto inverse = frame.inverseViewProjection * ndc;
                        require(
                            glm::length(glm::vec2(inverse) - point) < 0.00001f,
                            "Shader matrix inverse does not reproduce input coordinates."
                        );
                    }
                    const auto xCell =
                        *frame.worldToWindow(center + glm::vec2{GridMetrics::Spacing, 0});
                    const auto yCell =
                        *frame.worldToWindow(center + glm::vec2{0, GridMetrics::Spacing});
                    const double dx =
                        (xCell.x - middle.x) * surface.framebufferWidth / surface.windowWidth;
                    const double dy =
                        (middle.y - yCell.y) * surface.framebufferHeight / surface.windowHeight;
                    require(
                        std::abs(dx - dy) < 0.001 &&
                            std::abs(dx - GridMetrics::Spacing * frame.pixelsPerWorldUnit) < 0.001,
                        "Shared grid spacing or nonuniform DPI produced nonsquare framebuffer "
                        "cells."
                    );
                }
}

void viewportLayout()
{
    CanvasCamera camera;
    camera.setViewport(CanvasViewport{100, 50, 400, 300});
    auto frame = camera.frame({800, 600, 1600, 1200});
    auto pixels = frame.framebufferViewport;
    require(
        pixels.x == 200 && pixels.y == 500 && pixels.width == 800 && pixels.height == 600,
        "Logical viewport did not convert to bottom-left framebuffer pixels."
    );
    require(
        frame.viewport == CanvasViewport{100, 50, 400, 300},
        "Integer DPI layout lost logical bounds."
    );
    camera.setViewport(CanvasViewport{-50, -20, 450, 220});
    frame = camera.frame({800, 600, 1200, 900});
    pixels = frame.framebufferViewport;
    require(
        pixels.x == 0 && pixels.y == 600 && pixels.width == 600 && pixels.height == 300 &&
            frame.viewport == CanvasViewport{0, 0, 400, 200},
        "Partial viewport was not clipped consistently."
    );
    camera.setViewport(CanvasViewport{100.2, 50.2, 200.2, 100.2});
    frame = camera.frame({800, 600, 1200, 900});
    require(
        frame.viewport.contains(frame.viewport.x, frame.viewport.y) &&
            !frame.viewport.contains(frame.viewport.x + frame.viewport.width, frame.viewport.y),
        "Pixel-snapped viewport edges disagree with input inclusion."
    );
    const auto retained = camera.viewport();
    for (const auto bad :
         {CanvasViewport{0, 0, -1, 20},
          CanvasViewport{std::numeric_limits<double>::quiet_NaN(), 0, 20, 20}})
    {
        bool rejected = false;
        try
        {
            camera.setViewport(bad);
        }
        catch (const std::invalid_argument&)
        {
            rejected = true;
        }
        require(rejected && camera.viewport() == retained, "Invalid layout mutated camera state.");
    }
    for (float bad :
         {0.0f,
          -1.0f,
          std::numeric_limits<float>::infinity(),
          std::numeric_limits<float>::quiet_NaN()})
    {
        bool rejected = false;
        try
        {
            camera.setZoom(bad);
        }
        catch (const std::invalid_argument&)
        {
            rejected = true;
        }
        require(rejected && camera.zoom() == 1, "Invalid zoom mutated camera state.");
    }
    bool rejected = false;
    try
    {
        camera.setCenter({std::numeric_limits<float>::infinity(), 0});
    }
    catch (const std::invalid_argument&)
    {
        rejected = true;
    }
    require(rejected && camera.center() == glm::vec2{0}, "Invalid pan mutated camera state.");
    camera.setViewport(std::nullopt);
    for (auto surface :
         {CanvasSurface{0, 600, 800, 600},
          CanvasSurface{800, 0, 800, 600},
          CanvasSurface{800, 600, 0, 600},
          CanvasSurface{800, 600, 800, 0}})
    {
        const auto inactive = camera.frame(surface);
        require(
            !inactive.valid() && !inactive.windowToWorld({0, 0}) && !inactive.worldToWindow({0, 0}),
            "Minimized/empty canvas exposed a transform."
        );
    }
    for (auto bounds :
         {CanvasViewport{0, 0, 0, 600},
          CanvasViewport{900, 0, 10, 10},
          CanvasViewport{
              std::numeric_limits<double>::max(), 0, std::numeric_limits<double>::max(), 20
          }})
    {
        camera.setViewport(bounds);
        require(
            !camera.frame({800, 600, 800, 600}).valid(),
            "Empty/offscreen bounds produced a viewport."
        );
    }
    camera.setViewport(std::nullopt);
    camera.setZoom(std::numeric_limits<float>::denorm_min());
    require(!camera.frame({800, 600, 800, 600}).valid(), "Unrepresentable matrix reached drawing.");
}
} // namespace

int main(int argc, char** argv)
{
    try
    {
        const std::string group = argc > 1 ? argv[1] : "";
        if (group == "camera_transforms")
            transforms();
        else if (group == "camera_viewport_layout")
            viewportLayout();
        else
            throw std::runtime_error("Unknown camera test group.");
        std::cout << "PASS: " << group << '\n';
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
