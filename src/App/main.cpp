#include "App/Licenses.h"
#include "Engine.h"
#include "Graphics/RuntimeResources.h"

#include <exception>
#include <iostream>
#include <stdexcept>
#include <string_view>

#if defined(_WIN32) && LOGIC_SIMULATOR_PORTABLE
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif


namespace
{
int runApplication(bool checkRuntime, bool licensesOnly = false, bool printLicenses = false)
{
    try
    {
        if (printLicenses)
        {
            std::cout << RuntimeResources::licenseNotices();
            return 0;
        }
        if (licensesOnly)
        {
            showLicenseNotices(nullptr, RuntimeResources::licenseNotices());
            return 0;
        }
        Engine engine("Logic Gate Simulator", 800, 600);
        if (engine.init(!checkRuntime) != 0)
            throw std::runtime_error("Unable to initialize graphics. Use a graphics driver supporting OpenGL 3.3.");
        if (!checkRuntime)
            engine.run();
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
#if defined(_WIN32) && LOGIC_SIMULATOR_PORTABLE
        if (!checkRuntime && !printLicenses)
            MessageBoxA(nullptr, error.what(), "Logic Gate Simulator", MB_OK | MB_ICONERROR);
#endif
        return 1;
    }
}
} // namespace


#if defined(_WIN32) && LOGIC_SIMULATOR_PORTABLE
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR arguments, int)
{
    const std::wstring_view command(arguments);
    return runApplication(command == L"--check-runtime", command == L"--licenses", command == L"--print-licenses");
}
#else
int main(int argc, char** argv)
{
    const std::string_view command = argc == 2 ? argv[1] : "";
    return runApplication(command == "--check-runtime", command == "--licenses", command == "--print-licenses");
}
#endif
