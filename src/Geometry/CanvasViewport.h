#pragma once

#include <cmath>

/** Logical window coordinates: top-left origin, exclusive right/bottom edges. */
struct CanvasViewport
{
    double x = 0, y = 0, width = 0, height = 0;

    bool contains(double px, double py) const
    {
        return std::isfinite(px) && std::isfinite(py) && px >= x && py >= y && px < x + width &&
               py < y + height;
    }

    bool operator==(const CanvasViewport&) const = default;
};

/** Window coordinates and framebuffer pixels are independent on high-DPI displays. */
struct CanvasSurface
{
    int windowWidth = 0, windowHeight = 0;
    int framebufferWidth = 0, framebufferHeight = 0;
    bool operator==(const CanvasSurface&) const = default;
};

/** Integer framebuffer rectangle with OpenGL's bottom-left origin. */
struct PixelViewport
{
    int x = 0, y = 0, width = 0, height = 0;
};
