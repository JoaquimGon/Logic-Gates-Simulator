#pragma once

#include <cmath>
#include <cstdint>
#include <functional>

enum class UiInputKind
{
    Key,
    Text,
    MouseButton,
    Cursor,
    Scroll,
    WindowFocus
};

/** Native event values; key/button/action/modifier codes match GLFW without requiring its headers.
 */
struct UiInputEvent
{
    UiInputKind kind;
    int code = 0;
    int action = 0;
    int modifiers = 0;
    int scanCode = 0;
    double x = 0, y = 0;
    std::uint32_t codepoint = 0;
    bool focused = true;
};

struct UiInputCapture
{
    bool keyboard = false;
    bool pointer = false;
    bool operator==(const UiInputCapture&) const = default;
};

/** Input bounds in GLFW window coordinates, with top-left origin and exclusive right/bottom edges.
 */
struct CanvasInputBounds
{
    double x, y, width, height;

    bool contains(double px, double py) const
    {
        return std::isfinite(px) && std::isfinite(py) && px >= x && py >= y && px - x < width &&
               py - y < height;
    }

    bool operator==(const CanvasInputBounds&) const = default;
};

/** Called before canvas handling; true consumes this event. Persistent ownership uses setUiCapture.
 */
using UiInputHandler = std::function<bool(const UiInputEvent&)>;
