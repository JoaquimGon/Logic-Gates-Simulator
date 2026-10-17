#include "RuntimeResources.h"

#include <stdexcept>
#include <string>

#if LOGIC_SIMULATOR_PORTABLE
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace
{
#if LOGIC_SIMULATOR_PORTABLE
std::filesystem::path executableDirectory()
{
    std::wstring buffer(32768, L'\0');
    const DWORD size = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (size == 0 || size >= buffer.size())
        throw std::runtime_error("Cannot locate the application folder.");
    buffer.resize(size);
    return std::filesystem::path(buffer).parent_path();
}
#endif
} // namespace


std::filesystem::path RuntimeResources::assetPath(const std::filesystem::path& path)
{
    if (path.is_absolute())
        return path.lexically_normal();
#if LOGIC_SIMULATOR_PORTABLE
    return (executableDirectory() / "assets" / path).lexically_normal();
#else
    return (std::filesystem::path(PROJECT_ASSETS_DIR) / path).lexically_normal();
#endif
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
