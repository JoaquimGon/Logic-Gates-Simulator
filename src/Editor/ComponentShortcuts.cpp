#include "ComponentShortcuts.h"

#include <GLFW/glfw3.h>
#include <utility>

std::optional<EditOperation> componentShortcut(int key, GridCoords position)
{
    if (key == GLFW_KEY_1)
        return CreateInput{position, {0.15f, 0.15f}, "inputPin", false, PlacementPolicy::FindFree};
    if (key == GLFW_KEY_9)
        return CreateClock{position, {0.15f, 0.15f}, "clock", 1, PlacementPolicy::FindFree};
    if (key == GLFW_KEY_U || key == GLFW_KEY_I)
        return CreateLatch{
            key == GLFW_KEY_U ? LatchType::SR_LATCH : LatchType::D_LATCH,
            position,
            PlacementPolicy::FindFree
        };

    GateType type;
    const char* shader;
    switch (key)
    {
    case GLFW_KEY_2:
        type = NOT;
        shader = "NOTgate";
        break;
    case GLFW_KEY_3:
        type = AND;
        shader = "ANDgate";
        break;
    case GLFW_KEY_4:
        type = NAND;
        shader = "NANDgate";
        break;
    case GLFW_KEY_5:
        type = OR;
        shader = "ORgate";
        break;
    case GLFW_KEY_6:
        type = NOR;
        shader = "NORgate";
        break;
    case GLFW_KEY_7:
        type = XOR;
        shader = "XORgate";
        break;
    case GLFW_KEY_8:
        type = NXOR;
        shader = "NXORgate";
        break;
    default:
        return std::nullopt;
    }
    const bool inverted = type == NAND || type == NOR || type == NXOR;
    ComponentLayout layout;
    layout.shader = shader;
    if (type == NOT)
    {
        layout.size = {0.2f, 0.1f};
        layout.inputs = {{PinType::INPUT, 0, PinState::DISCONNECTED, {-2, 0}}};
        layout.outputs = {{PinType::OUTPUT, 0, PinState::DISCONNECTED, {1, 0}}};
    }
    else
    {
        layout.size = {inverted ? 0.3f : 0.2f, 0.2f};
        layout.inputs = {
            {PinType::INPUT, 0, PinState::DISCONNECTED, {-2, 1}},
            {PinType::INPUT, 1, PinState::DISCONNECTED, {-2, -1}}
        };
        layout.outputs = {{PinType::OUTPUT, 0, PinState::DISCONNECTED, {inverted ? 3 : 2, 0}}};
    }
    return CreateGate{type, position, std::move(layout), PlacementPolicy::FindFree};
}
