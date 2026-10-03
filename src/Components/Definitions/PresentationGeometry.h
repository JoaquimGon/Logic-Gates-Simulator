#pragma once

#include <array>

/** Silhouette metadata, independent of logical behavior and graphics resources. */
enum class BodyContour
{
    Box,
    And,
    Or,
    Xor,
    Not,
    Input,
    Clock,
    Output
};

struct BodyStyle
{
    BodyContour contour = BodyContour::Box;
    bool inverted = false;
    std::array<float, 4> tint{1, 1, 1, 1};
};

/** Floating-point body extents; the grid origin and pin anchors are independent. */
struct BodyBounds
{
    float left, bottom, right, top;

    float width() const { return right - left; }

    float height() const { return top - bottom; }

    float centerX() const { return (left + right) * 0.5f; }

    float centerY() const { return (bottom + top) * 0.5f; }
};

/** @brief Visible normalized bounds, including bubbles and decorative XOR arcs. */
BodyBounds normalizedBodyBounds(const BodyStyle& style);
/** @brief Scales visible bounds about the grid origin, without rounding dimensions or pins. */
BodyBounds bodyBounds(const BodyStyle& style, float x, float y, float width, float height);

/** @brief Signed silhouette distance in normalized body coordinates, excluding inner glyphs. */
float bodyContourDistance(const BodyStyle& style, float x, float y);

/** @brief First axis-aligned silhouette contact; unavailable rows use a safe inner row. */
std::array<float, 2> bodyContact(const BodyStyle& style, float x, float y);

/** @brief Native gates attach horizontally; boxes/source symbols follow their nearest side. */
bool usesHorizontalContact(const BodyStyle& style, float x, float y);
