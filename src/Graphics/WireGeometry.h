#pragma once

#include <vector>

class Wire;

/**
 * @brief Builds colored triangle vertices from a wire without modifying its geometry.
 * @param wire Committed or preview wire with its current signal state.
 * @return Interleaved position (3 floats) and color (4 floats) vertices.
 */
std::vector<float> buildWireVertices(const Wire& wire);
