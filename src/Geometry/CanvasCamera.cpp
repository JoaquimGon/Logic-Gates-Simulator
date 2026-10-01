#include "Geometry/CanvasCamera.h"

#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <stdexcept>

void CanvasCamera::setCenter(glm::vec2 center)
{
    if (!std::isfinite(center.x) || !std::isfinite(center.y))
        throw std::invalid_argument("Camera center must be finite.");
    m_center = center;
}

void CanvasCamera::setZoom(float zoom)
{
    if (!std::isfinite(zoom) || zoom <= 0)
        throw std::invalid_argument("Camera zoom must be finite and positive.");
    m_zoom = zoom;
}

void CanvasCamera::setViewport(std::optional<CanvasViewport> viewport)
{
    if (viewport && (!std::isfinite(viewport->x) || !std::isfinite(viewport->y) ||
                     !std::isfinite(viewport->width) || !std::isfinite(viewport->height) ||
                     viewport->width < 0 || viewport->height < 0))
        throw std::invalid_argument("Canvas viewport must be finite with nonnegative dimensions.");
    m_viewport = viewport;
}

CanvasCameraFrame CanvasCamera::frame(CanvasSurface surface) const
{
    CanvasCameraFrame result;
    result.surface = surface;
    if (surface.windowWidth <= 0 || surface.windowHeight <= 0 || surface.framebufferWidth <= 0 ||
        surface.framebufferHeight <= 0)
        return result;
    const double width = surface.windowWidth, height = surface.windowHeight;
    const auto requested = m_viewport.value_or(CanvasViewport{0, 0, width, height});
    const double scaleX = surface.framebufferWidth / width;
    const double scaleY = surface.framebufferHeight / height;
    // Clip before rounding/casting so even large finite layout requests cannot overflow integers.
    const int left = static_cast<int>(std::lround(std::clamp(requested.x, 0.0, width) * scaleX));
    const int top = static_cast<int>(std::lround(std::clamp(requested.y, 0.0, height) * scaleY));
    const int right = static_cast<int>(
        std::lround(std::clamp(requested.x + requested.width, 0.0, width) * scaleX)
    );
    const int bottom = static_cast<int>(
        std::lround(std::clamp(requested.y + requested.height, 0.0, height) * scaleY)
    );
    if (right <= left || bottom <= top)
        return result;
    result.framebufferViewport = {
        left, surface.framebufferHeight - bottom, right - left, bottom - top
    };
    // Use the same pixel-snapped effective edges for input and drawing.
    result.viewport = {
        left / scaleX, top / scaleY, (right - left) / scaleX, (bottom - top) / scaleY
    };
    const float aspect = static_cast<float>(right - left) / (bottom - top);
    glm::mat4 projection{1};
    projection[0][0] = m_zoom / aspect;
    projection[1][1] = m_zoom;
    result.viewProjection = projection * glm::translate(glm::mat4{1}, glm::vec3{-m_center, 0});
    glm::mat4 inverseProjection{1};
    inverseProjection[0][0] = aspect / m_zoom;
    inverseProjection[1][1] = 1 / m_zoom;
    result.inverseViewProjection =
        glm::translate(glm::mat4{1}, glm::vec3{m_center, 0}) * inverseProjection;
    result.pixelsPerWorldUnit = m_zoom * (bottom - top) * 0.5f;
    for (int column = 0; column < 4; ++column)
        for (int row = 0; row < 4; ++row)
            if (!std::isfinite(result.viewProjection[column][row]) ||
                !std::isfinite(result.inverseViewProjection[column][row]))
                return CanvasCameraFrame{surface};
    if (!std::isfinite(result.pixelsPerWorldUnit) || result.pixelsPerWorldUnit <= 0)
        return CanvasCameraFrame{surface};
    return result;
}

std::optional<glm::vec2> CanvasCameraFrame::windowToWorld(glm::dvec2 point) const
{
    if (!valid() || !std::isfinite(point.x) || !std::isfinite(point.y))
        return std::nullopt;
    const glm::vec4 ndc{
        static_cast<float>((point.x - viewport.x) * 2 / viewport.width - 1),
        static_cast<float>(1 - (point.y - viewport.y) * 2 / viewport.height),
        0,
        1
    };
    const glm::vec2 world = inverseViewProjection * ndc;
    if (!std::isfinite(world.x) || !std::isfinite(world.y))
        return std::nullopt;
    return world;
}

std::optional<glm::dvec2> CanvasCameraFrame::worldToWindow(glm::vec2 point) const
{
    if (!valid() || !std::isfinite(point.x) || !std::isfinite(point.y))
        return std::nullopt;
    const glm::vec4 ndc = viewProjection * glm::vec4{point, 0, 1};
    if (!std::isfinite(ndc.x) || !std::isfinite(ndc.y))
        return std::nullopt;
    return glm::dvec2{
        viewport.x + (ndc.x + 1.0) * viewport.width * 0.5,
        viewport.y + (1.0 - ndc.y) * viewport.height * 0.5
    };
}
