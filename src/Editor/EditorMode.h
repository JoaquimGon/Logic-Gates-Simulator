#pragma once

enum class EditorMode
{
    Selection,
    Interaction
};

inline const char* editorModeName(EditorMode mode)
{
    return mode == EditorMode::Selection ? "Selection" : "Interaction";
}
