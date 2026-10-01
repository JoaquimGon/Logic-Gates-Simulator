#pragma once

#include "Components/Views/ComponentView.h"
#include "Graphics/Presentation/ComponentRenderData.h"

#include <memory>
#include <unordered_map>

/** @brief Adapts common view metadata once per frame, including presentation-only move previews. */
std::vector<ComponentRenderData>
buildComponentPresentation(const std::unordered_map<int, std::unique_ptr<ComponentView>>& views);
