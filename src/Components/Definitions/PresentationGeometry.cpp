#include "Components/Definitions/PresentationGeometry.h"

#include <algorithm>
#include <cmath>

namespace
{
float circle(float x, float y, float cx, float cy, float radius)
{
    return std::hypot(x - cx, y - cy) - radius;
}

float box(float x, float y, float hx, float hy, float radius)
{
    const float dx = std::abs(x) - hx + radius;
    const float dy = std::abs(y) - hy + radius;
    return std::min(std::max(dx, dy), 0.0f) + std::hypot(std::max(dx, 0.0f), std::max(dy, 0.0f)) -
           radius;
}
} // namespace

BodyBounds normalizedBodyBounds(const BodyStyle& style)
{
    // These extents match the native SDFs; the shader quad can include empty margins.
    BodyBounds bounds{-0.48f, -0.48f, 0.48f, 0.48f};
    switch (style.contour)
    {
    case BodyContour::And:
        bounds = {-0.5f, -0.42f, 0.5f, 0.42f};
        break;
    case BodyContour::Or:
        // The left extent is the back-cut/lens intersection; the tip joins both circles.
        bounds = {-0.499590f, -0.4f, -0.334f + std::sqrt(1.05f * 1.05f - 0.65f * 0.65f), 0.4f};
        break;
    case BodyContour::Xor:
        bounds = {
            -1.27f + std::sqrt(0.847f * 0.847f - 0.38f * 0.38f),
            -0.4f,
            -0.334f + std::sqrt(1.05f * 1.05f - 0.65f * 0.65f),
            0.4f
        };
        break;
    case BodyContour::Not:
        return {-0.5f, -0.5f, 0.25f, 0.5f};
    case BodyContour::Input:
        return {-0.42f / 1.3f, -0.42f / 1.3f, 0.42f / 1.3f, 0.42f / 1.3f};
    case BodyContour::Clock:
        return {-0.47f / 1.3f, -0.47f / 1.3f, 0.47f / 1.3f, 0.47f / 1.3f};
    case BodyContour::Output:
        return {-0.32f, -0.32f, 0.32f, 0.32f};
    default:
        return bounds;
    }
    if (style.inverted)
    {
        bounds.left /= 1.5f;
        bounds.right = (0.615f + 0.13f) / 1.5f;
    }
    // Geometry outside the shader quad is clipped (notably the plain XOR's arc).
    bounds.left = std::max(bounds.left, -0.5f);
    bounds.right = std::min(bounds.right, 0.5f);
    return bounds;
}

BodyBounds bodyBounds(const BodyStyle& style, float x, float y, float width, float height)
{
    const auto bounds = normalizedBodyBounds(style);
    return {
        x + bounds.left * width,
        y + bounds.bottom * height,
        x + bounds.right * width,
        y + bounds.top * height
    };
}

float bodyContourDistance(const BodyStyle& style, float x, float y)
{
    if (style.inverted)
        x *= 1.5f;
    float d;
    switch (style.contour)
    {
    case BodyContour::And:
        d = x > 0.08f ? circle(x, y, 0.08f, 0, 0.42f) : box(x + 0.21f, y, 0.29f, 0.42f, 0);
        break;
    case BodyContour::Or:
    case BodyContour::Xor:
    {
        const float backX = style.contour == BodyContour::Xor ? -1.27f : -1.4f;
        d = std::max(
            std::max(circle(x, y, -0.334f, 0.65f, 1.05f), circle(x, y, -0.334f, -0.65f, 1.05f)),
            -circle(x, y, backX, 0, 0.98f)
        );
        // Contacts target the solid body; the XOR's separate decorative arc is not an anchor.
        break;
    }
    case BodyContour::Not:
        y *= 0.5f;
        return std::min(
            std::max(-x - 0.5f, std::abs(y) + 0.5f * x), circle(x, y, 0.125f, 0, 0.125f)
        );
    case BodyContour::Input:
        return box(x * 1.3f, y * 1.3f, 0.42f, 0.42f, 0.14f);
    case BodyContour::Clock:
        return box(x * 1.3f, y * 1.3f, 0.47f, 0.47f, 0.05f);
    case BodyContour::Output:
        return circle(x, y, 0, 0, 0.32f);
    default:
        return box(x, y, 0.48f, 0.48f, 0.05f);
    }
    return style.inverted ? std::min(d, circle(x, y, 0.615f, 0, 0.13f)) : d;
}

bool usesHorizontalContact(const BodyStyle& style, float x, float y)
{
    return style.contour == BodyContour::And || style.contour == BodyContour::Or ||
           style.contour == BodyContour::Xor || style.contour == BodyContour::Not ||
           std::abs(x) >= std::abs(y);
}

std::array<float, 2> bodyContact(const BodyStyle& style, float x, float y)
{
    if (bodyContourDistance(style, x, y) <= 0)
        return {x, y};
    const bool horizontal = usesHorizontalContact(style, x, y);
    const float varying = horizontal ? x : y;
    const float fixed = horizontal ? y : x;
    const float sign = varying < 0 ? -1.0f : 1.0f;
    const float start = std::clamp(varying, -0.75f, 0.75f);
    const float end = -sign * 0.75f;
    auto distance = [&](float along, float row)
    {
        return horizontal ? bodyContourDistance(style, along, row)
                          : bodyContourDistance(style, row, along);
    };
    // First try the exact pin row. Extreme rows get a dogleg into a safe inner band.
    for (const float row : {fixed, std::clamp(fixed, -0.3f, 0.3f), 0.0f})
    {
        float outside = start;
        for (int step = 1; step <= 256; ++step)
        {
            float inside = start + (end - start) * static_cast<float>(step) / 256;
            if (distance(inside, row) <= 0)
            {
                for (int iteration = 0; iteration < 24; ++iteration)
                {
                    const float middle = (outside + inside) * 0.5f;
                    if (distance(middle, row) > 0)
                        outside = middle;
                    else
                        inside = middle;
                }
                return horizontal ? std::array<float, 2>{inside, row}
                                  : std::array<float, 2>{row, inside};
            }
            outside = inside;
        }
    }
    return {0, 0};
}
