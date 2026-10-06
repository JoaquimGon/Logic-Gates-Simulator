#include "App/FileDialog.h"

#include <algorithm>
#include <iterator>
#include <stdexcept>
#include <string>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#include <commdlg.h>
#endif

std::optional<std::filesystem::path> chooseCircuitFile(
    GLFWwindow* window, bool saving, const std::filesystem::path& current, bool subcircuit
)
{
#ifdef _WIN32
    wchar_t path[32768]{};
    const auto suggested = current.empty()
                               ? std::wstring{subcircuit ? L"subcircuit.json" : L"circuit.json"}
                               : current.wstring();
    if (suggested.size() >= std::size(path))
        throw std::invalid_argument("Circuit file path is too long.");
    if (saving)
        std::copy(suggested.begin(), suggested.end(), path);
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = glfwGetWin32Window(window);
    dialog.lpstrFilter = L"Circuit JSON (*.json)\0*.json\0All files (*.*)\0*.*\0\0";
    dialog.lpstrFile = path;
    dialog.nMaxFile = static_cast<DWORD>(std::size(path));
    dialog.lpstrDefExt = L"json";
    dialog.lpstrTitle = subcircuit ? (saving ? L"Save subcircuit" : L"Load subcircuit")
                                   : (saving ? L"Save Main circuit" : L"Open Main circuit");
    dialog.Flags =
        OFN_NOCHANGEDIR | OFN_PATHMUSTEXIST | (saving ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
    if (saving ? GetSaveFileNameW(&dialog) : GetOpenFileNameW(&dialog))
        return std::filesystem::path{path};
    const auto error = CommDlgExtendedError();
    if (error != 0)
        throw std::runtime_error("File dialog failed (" + std::to_string(error) + ").");
    return std::nullopt;
#else
    throw std::runtime_error("Native file dialogs are currently available on Windows only.");
#endif
}
