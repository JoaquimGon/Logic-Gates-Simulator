#include "App/FileDialog.h"

#include <algorithm>
#include <cstdint>
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
#include <shellapi.h>
#include <shobjidl.h>
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
    dialog.hwndOwner = window ? glfwGetWin32Window(window) : nullptr;
    dialog.lpstrFilter = L"Circuit JSON (*.json)\0*.json\0All files (*.*)\0*.*\0\0";
    dialog.lpstrFile = path;
    dialog.nMaxFile = static_cast<DWORD>(std::size(path));
    dialog.lpstrDefExt = L"json";
    dialog.lpstrTitle = subcircuit ? (saving ? L"Save subcircuit" : L"Load subcircuit")
                                   : (saving ? L"Save workspace" : L"Open workspace");
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

std::optional<std::filesystem::path> chooseStorageFolder(GLFWwindow* window)
{
#ifdef _WIN32
    const auto initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(initialized))
        throw std::runtime_error("Cannot initialize the storage folder picker.");
    IFileOpenDialog* dialog = nullptr;
    auto result = CoCreateInstance(
        CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)
    );
    std::optional<std::filesystem::path> chosen;
    if (SUCCEEDED(result))
    {
        DWORD options = 0;
        result = dialog->GetOptions(&options);
        if (SUCCEEDED(result))
            result = dialog->SetOptions(
                options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_NOCHANGEDIR
            );
        dialog->SetTitle(L"Choose subcircuit storage folder (existing files are copied)");
        if (SUCCEEDED(result))
            result = dialog->Show(window ? glfwGetWin32Window(window) : nullptr);
        if (SUCCEEDED(result))
        {
            IShellItem* item = nullptr;
            result = dialog->GetResult(&item);
            if (SUCCEEDED(result))
            {
                PWSTR path = nullptr;
                result = item->GetDisplayName(SIGDN_FILESYSPATH, &path);
                if (SUCCEEDED(result))
                {
                    chosen = std::filesystem::path(path);
                    CoTaskMemFree(path);
                }
                item->Release();
            }
        }
        dialog->Release();
    }
    CoUninitialize();
    if (FAILED(result) && result != HRESULT_FROM_WIN32(ERROR_CANCELLED))
        throw std::runtime_error("Storage folder picker failed.");
    return chosen;
#else
    throw std::runtime_error("Native folder dialogs are currently available on Windows only.");
#endif
}

void openStorageFolder(const std::filesystem::path& folder)
{
#ifdef _WIN32
    const auto result =
        ShellExecuteW(nullptr, L"open", folder.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    if (reinterpret_cast<std::intptr_t>(result) <= 32)
        throw std::runtime_error("Cannot open the storage folder in Explorer.");
#else
    throw std::runtime_error("Opening storage in Explorer is currently available on Windows only.");
#endif
}
