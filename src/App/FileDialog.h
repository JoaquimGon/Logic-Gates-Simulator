#pragma once

#include <filesystem>
#include <optional>

struct GLFWwindow;

/** @brief Native JSON-file picker; cancellation returns no path, dialog failures throw. */
std::optional<std::filesystem::path>
chooseCircuitFile(GLFWwindow* window, bool saving, const std::filesystem::path& current = {}, bool subcircuit = false);

std::optional<std::filesystem::path> chooseStorageFolder(GLFWwindow* window);

void openStorageFolder(const std::filesystem::path& folder);
