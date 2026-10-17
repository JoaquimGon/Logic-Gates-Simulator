#include "Graphics/RuntimeResources.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

int main()
{
    namespace fs = std::filesystem;
    const auto original = fs::current_path();
    const auto directory =
        fs::temp_directory_path() /
        ("logic-resources-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    struct Cleanup
    {
        fs::path original, temporary;

        ~Cleanup()
        {
            std::error_code ignored;
            fs::current_path(original, ignored);
            fs::remove_all(temporary, ignored);
        }
    } cleanup{original, directory};
    try
    {
        const auto shader = RuntimeResources::assetPath("shaders/grid.frag");
        const auto font = RuntimeResources::fontPath();
        const auto source = RuntimeResources::shaderSource(shader);
        if (source.find("#version") == std::string::npos || !fs::is_regular_file(font))
            throw std::runtime_error("Required shader or font is unavailable.");
        fs::create_directories(directory / "shaders");
        std::ofstream(directory / "shaders/grid.frag") << "An unrelated file must not replace application shaders.";
        fs::current_path(directory);
        if (shader != RuntimeResources::assetPath("shaders/grid.frag") || font != RuntimeResources::fontPath() ||
            source != RuntimeResources::shaderSource("shaders/grid.frag"))
            throw std::runtime_error("Resource resolution depends on the working directory.");
        const auto absolute = directory / "shaders/grid.frag";
        if (RuntimeResources::assetPath(absolute) != absolute ||
            RuntimeResources::shaderSource(absolute).find("unrelated file") == std::string::npos)
            throw std::runtime_error("An explicit absolute resource path was changed.");
        if (RuntimeResources::assetPath("shaders/../shaders/grid.frag") != shader)
            throw std::runtime_error("Relative resource paths were not normalized.");
        bool rejectedMissing = false;
        try
        {
            RuntimeResources::shaderSource("shaders/missing-shader.frag");
        }
        catch (const std::runtime_error&)
        {
            rejectedMissing = true;
        }
        if (!rejectedMissing)
            throw std::runtime_error("A missing shader was accepted.");
        for (const auto name : {"MIT License", "GLFW", "GLM", "nlohmann", "Apache License", "stb_truetype"})
            if (RuntimeResources::licenseNotices().find(name) == std::string_view::npos)
                throw std::runtime_error("The embedded license notices are incomplete.");
        fs::current_path(original);
        std::cout << "Shaders and licenses are available independently of the working directory.\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        fs::current_path(original);
        std::cerr << error.what() << '\n';
        return 1;
    }
}
