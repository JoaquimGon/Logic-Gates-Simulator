#pragma once

#include <filesystem>
#include <optional>

struct GLFWwindow;

/** Native JSON-file picker; cancellation returns no path, dialog failures throw. */
std::optional<std::filesystem::path>
chooseCircuitFile(GLFWwindow* window, bool saving, const std::filesystem::path& current = {});
