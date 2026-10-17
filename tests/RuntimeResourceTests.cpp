#include "Graphics/RuntimeResources.h"

#include <filesystem>
#include <iostream>
#include <stdexcept>

int main()
{
    namespace fs = std::filesystem;
    const auto original = fs::current_path();
    try
    {
        const auto shader = RuntimeResources::assetPath("shaders/grid.frag");
        const auto font = RuntimeResources::fontPath();
        if (!shader.is_absolute() || !fs::is_regular_file(shader) || !fs::is_regular_file(font))
            throw std::runtime_error("Required shader or font was not resolved to an existing file.");
        fs::current_path(fs::temp_directory_path());
        if (shader != RuntimeResources::assetPath("shaders/grid.frag") || font != RuntimeResources::fontPath())
            throw std::runtime_error("Resource resolution depends on the working directory.");
        if (RuntimeResources::assetPath(shader) != shader)
            throw std::runtime_error("An explicit absolute resource path was changed.");
        if (RuntimeResources::assetPath("shaders/../shaders/grid.frag") != shader)
            throw std::runtime_error("Relative resource paths were not normalized.");
        fs::current_path(original);
        std::cout << "Resource paths remain valid after changing the working directory.\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        fs::current_path(original);
        std::cerr << error.what() << '\n';
        return 1;
    }
}
