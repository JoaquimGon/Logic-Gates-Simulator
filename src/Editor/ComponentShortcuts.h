#pragma once

#include "Actions/EditTypes.h"

#include <optional>

/** @brief Maps existing keyboard accelerators to native creation requests. */
std::optional<EditOperation> componentShortcut(int key, GridCoords position);
