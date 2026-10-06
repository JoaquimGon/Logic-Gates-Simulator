#pragma once
#include "UI/UI.h"

#include <filesystem>
class CircuitViews;
class Input;
struct GLFWwindow;

/** Runs a queued File command after input callbacks; cancellation preserves file associations. */
void performFileCommand(
    UI::FileCommand command,
    GLFWwindow* window,
    UI& ui,
    CircuitViews& views,
    Input& input,
    std::filesystem::path& mainFile
);
/** Applies a chosen path; a cancelled picker has no side effects. */
void applyFileCommand(
    UI::FileCommand command,
    const std::optional<std::filesystem::path>& chosen,
    UI& ui,
    CircuitViews& views,
    Input& input,
    std::filesystem::path& mainFile
);
