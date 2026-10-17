#pragma once

#include <filesystem>

namespace RuntimeResources
{
/** @brief Resolve assets independently of the current directory; portable builds use the executable's folder. */
std::filesystem::path assetPath(const std::filesystem::path& path);

/** @brief Portable builds use an installed Windows font rather than redistributing a font file. */
std::filesystem::path fontPath();
} // namespace RuntimeResources
