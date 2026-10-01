#pragma once
#include "Graphics/Presentation/ComponentRenderData.h"
#include "Graphics/Text/TextGeometry.h"

/** @brief Fits body/pin labels inside each body; pin placement follows the nearest body side. */
std::vector<TextRun>
layoutComponentLabels(std::span<const ComponentRenderData> components, const FontMetrics& font);
