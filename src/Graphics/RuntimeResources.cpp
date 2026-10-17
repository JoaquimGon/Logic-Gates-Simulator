#include "RuntimeResources.h"

#include "EmbeddedResources.h"

#include <fstream>
#include <stdexcept>
#include <string>

#if LOGIC_SIMULATOR_PORTABLE
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

std::filesystem::path RuntimeResources::assetPath(const std::filesystem::path& path)
{
    if (path.is_absolute())
        return path.lexically_normal();
#if LOGIC_SIMULATOR_PORTABLE
    return path.lexically_normal();
#else
    return (std::filesystem::path(PROJECT_ASSETS_DIR) / path).lexically_normal();
#endif
}


bool RuntimeResources::isEmbeddedAsset(const std::filesystem::path& path)
{
#if LOGIC_SIMULATOR_PORTABLE
    return path.is_relative();
#else
    return false;
#endif
}


std::string RuntimeResources::shaderSource(const std::filesystem::path& path)
{
    const auto resolved = assetPath(path);
#if LOGIC_SIMULATOR_PORTABLE
    if (isEmbeddedAsset(resolved))
    {
        const auto name = resolved.generic_string();
        for (const auto& [key, source] : EmbeddedResources::shaders)
            if (key == name)
                return std::string(source);
        throw std::runtime_error("Unknown embedded shader: " + name);
    }
#endif
    std::ifstream file(resolved, std::ios::binary);
    if (!file)
        throw std::runtime_error("Cannot read shader: " + resolved.string());
    std::string source{std::istreambuf_iterator<char>(file), {}};
    if (file.bad())
        throw std::runtime_error("Failed reading shader: " + resolved.string());
    return source;
}


std::string_view RuntimeResources::licenseNotices()
{
    return EmbeddedResources::notices;
}


std::filesystem::path RuntimeResources::fontPath()
{
#if LOGIC_SIMULATOR_PORTABLE
    std::wstring buffer(32768, L'\0');
    const UINT size = GetWindowsDirectoryW(buffer.data(), static_cast<UINT>(buffer.size()));
    if (size == 0 || size >= buffer.size())
        throw std::runtime_error("Cannot locate the Windows fonts folder.");
    buffer.resize(size);
    const auto fonts = std::filesystem::path(buffer) / "Fonts";
    for (const auto* name : {L"consola.ttf", L"cour.ttf", L"arial.ttf"})
        if (const auto font = fonts / name; std::filesystem::is_regular_file(font))
            return font;
    throw std::runtime_error("No supported Windows font was found (Consolas, Courier New or Arial).");
#else
    return PROJECT_FONT_PATH;
#endif
}
