#pragma once
#include "Components/Definitions/PresentationGeometry.h"

#include <vector>

class Wire;

/**
 * @brief Builds colored triangle vertices from a wire without modifying its geometry.
 * @param wire Committed or preview wire with its current signal state.
 * @return Interleaved position (3 floats) and color (4 floats) vertices.
 */
std::vector<float> buildWireVertices(const Wire& wire);
/** Appends into reusable staging storage, retaining existing contents. */
void appendWireVertices(const Wire& wire, std::vector<float>& data);
inline constexpr float WireHalfWidth = 0.006f;

/** @brief Rounded outline triangles, with a stroke half the width of electrical wires. */
std::vector<float> buildBoundsVertices(BodyBounds bounds, float padding, float alpha);
