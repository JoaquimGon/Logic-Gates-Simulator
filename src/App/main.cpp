#include "Engine.h"

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
int runApplication(bool checkRuntime)
{
    try
    {
        Engine engine("Logic Gate Simulator", 800, 600);
        if (engine.init(!checkRuntime) != 0)
            throw std::runtime_error(
                "Unable to initialize graphics. Keep the assets folder next to LogicGateSimulator.exe "
                "and use a graphics driver supporting OpenGL 3.3."
            );
        if (!checkRuntime)
            engine.run();
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
#if defined(_WIN32) && LOGIC_SIMULATOR_PORTABLE
        if (!checkRuntime)
            MessageBoxA(nullptr, error.what(), "Logic Gate Simulator", MB_OK | MB_ICONERROR);
#endif
        return 1;
    }
}
} // namespace


#if defined(_WIN32) && LOGIC_SIMULATOR_PORTABLE
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR arguments, int)
{
    return runApplication(std::wstring_view(arguments) == L"--check-runtime");
}
#else
int main(int argc, char** argv)
{
    return runApplication(argc == 2 && std::string_view(argv[1]) == "--check-runtime");
}
#endif
