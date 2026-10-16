#pragma once

#include <span>
#include <string_view>

class Scene;

enum class CircuitExample
{
    LogicGates,
    HalfAdder,
    FullAdder,
    Counter
};

struct CircuitExampleInfo
{
    CircuitExample id;
    std::string_view name, description;
};

std::span<const CircuitExampleInfo> circuitExamples();

/** @brief Fresh editable geometry, built and settled before it is opened in the editor. */
Scene buildExampleCircuit(CircuitExample example);
