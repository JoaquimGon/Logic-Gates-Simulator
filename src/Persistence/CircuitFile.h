#pragma once

#include "Editor/Scene.h"

#include <filesystem>
#include <string>
#include <string_view>

struct SavedCircuit
{
    std::string name;
    Scene scene;
};

/** @brief Circuit design only: committed geometry, settings and manual inputs; no timer/latch snapshot. */
std::string circuitToJson(const Scene& scene, const std::string& name);

/** @brief Builds an isolated scene through EditorActions. Invalid files throw without touching the editor.
 */
SavedCircuit circuitFromJson(std::string_view text);

/** @brief Writes a temporary sibling and replaces the destination only after a complete write. */
void saveCircuit(const std::filesystem::path& path, const Scene& scene, const std::string& name);

SavedCircuit loadCircuit(const std::filesystem::path& path);

struct SavedSubcircuit
{
    DefinitionIdentity identity;
    SavedCircuit design;
};

/** @brief Serialize a reusable design together with its versioned identity and explicit interface mapping. */
std::string subcircuitToJson(const Scene& scene, DefinitionIdentity identity, const std::string& name);

/** @brief Recover a reusable design and verify its interface against the reconstructed scene. */
SavedSubcircuit subcircuitFromJson(std::string_view text);

void saveSubcircuit(
    const std::filesystem::path& path, const Scene& scene, DefinitionIdentity identity, const std::string& name
);

SavedSubcircuit loadSubcircuit(const std::filesystem::path& path);

/** @brief Reuses the circuit writer for the small library location settings file. */
void writeTextAtomically(const std::filesystem::path& path, const std::string& text);
