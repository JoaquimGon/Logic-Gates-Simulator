#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace RuntimeResources
{
/** @brief Normalize a shader identifier; development builds resolve relative paths against the source assets. */
std::filesystem::path assetPath(const std::filesystem::path& path);

/** @brief Relative shader identifiers select immutable embedded resources in standalone builds. */
bool isEmbeddedAsset(const std::filesystem::path& path);

/** @brief Read embedded shaders or explicit files, retaining file-based loading for development and reload tests. */
std::string shaderSource(const std::filesystem::path& path);

/** @brief Project and dependency notices included in the application and its license viewer. */
std::string_view licenseNotices();

/** @brief Portable builds use an installed Windows font rather than redistributing a font file. */
std::filesystem::path fontPath();
} // namespace RuntimeResources
