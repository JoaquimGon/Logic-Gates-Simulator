#pragma once
#include "Editor/Scene.h"

/** @brief Empty on success; packaging needs unique names within each pin direction. */
std::string subcircuitProblem(const Scene& scene);

/** @brief Clock names include saved nested instances, qualified by their containing box. */
std::vector<std::string> subcircuitClockNames(const Scene& scene);

/** @brief Freezes an authored scene and compiles its external ports in component creation order. */
ComponentDefinition makeSubcircuit(const Scene& scene, DefinitionIdentity identity, const std::string& name);

std::string newSubcircuitId();
