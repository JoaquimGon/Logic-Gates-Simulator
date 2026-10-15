#pragma once
#include "UI/UI.h"

#include <filesystem>
class CircuitViews;
class Input;
class SubcircuitLibrary;
struct GLFWwindow;

/** Runs a queued File command. False means a cancelled picker or no operation. */
bool performFileCommand(
    UI::FileCommand command,
    GLFWwindow* window,
    UI& ui,
    CircuitViews& views,
    Input& input,
    SubcircuitLibrary* library
);
/** Applies a chosen path; a cancelled picker has no side effects. */
bool applyFileCommand(
    UI::FileCommand command,
    const std::optional<std::filesystem::path>& chosen,
    UI& ui,
    CircuitViews& views,
    Input& input,
    SubcircuitLibrary* library
);
/** Reads managed designs once, preserving embedded definitions in existing scenes. */
void refreshSubcircuitLibrary(SubcircuitLibrary& library, CircuitViews& views, UI& ui);
/** Opens an example in a fresh workspace, preserving all existing tabs. */
void openExampleCircuit(
    CircuitExample example, UI& ui, CircuitViews& views, Input& input, CanvasSurface surface
);
