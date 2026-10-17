#include "App/Licenses.h"

#include <iostream>
#include <stdexcept>
#include <string>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

namespace
{
/** @brief Keep the read-only notice text fitted to its resizable owner window. */
LRESULT CALLBACK licenseWindowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == WM_SIZE)
    {
        RECT bounds{};
        GetClientRect(window, &bounds);
        MoveWindow(
            GetDlgItem(window, 1), 8, 8, bounds.right > 16 ? bounds.right - 16 : 0,
            bounds.bottom > 16 ? bounds.bottom - 16 : 0, TRUE
        );
        return 0;
    }
    if (message == WM_CLOSE)
    {
        DestroyWindow(window);
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}
} // namespace
#endif


void showLicenseNotices(GLFWwindow* parent, std::string_view notices)
{
#ifdef _WIN32
    std::string text;
    for (const char c : notices)
    {
        if (c == '\r')
            continue;
        if (c == '\n')
            text += '\r';
        text += c;
    }
    const auto size =
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (size == 0)
        throw std::runtime_error("Cannot decode the license notices.");
    std::wstring wide(size, L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), wide.data(), size);

    const auto instance = GetModuleHandleW(nullptr);
    constexpr auto className = L"LogicGateSimulatorLicenses";
    WNDCLASSW type{};
    type.lpfnWndProc = licenseWindowProcedure;
    type.hInstance = instance;
    type.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    type.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    type.lpszClassName = className;
    if (!RegisterClassW(&type) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        throw std::runtime_error("Cannot initialize the license viewer.");

    const HWND owner = parent ? glfwGetWin32Window(parent) : nullptr;
    const auto window = CreateWindowExW(
        WS_EX_DLGMODALFRAME, className, L"About / Licenses - Logic Gate Simulator", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT,
        CW_USEDEFAULT, 860, 640, owner, nullptr, instance, nullptr
    );
    if (!window)
        throw std::runtime_error("Cannot open the license viewer.");
    const auto edit = CreateWindowExW(
        WS_EX_CLIENTEDGE, L"EDIT", wide.c_str(),
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL, 8, 8, 0, 0, window,
        reinterpret_cast<HMENU>(1), instance, nullptr
    );
    if (!edit)
    {
        DestroyWindow(window);
        throw std::runtime_error("Cannot display the license notices.");
    }
    SendMessageW(edit, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
    SendMessageW(window, WM_SIZE, 0, 0);
    if (owner)
        EnableWindow(owner, FALSE);
    ShowWindow(window, SW_SHOW);
    SetFocus(edit);

    // Keep this dialog modal without posting a quit message into the simulator's event loop.
    MSG message{};
    while (IsWindow(window))
    {
        const auto result = GetMessageW(&message, nullptr, 0, 0);
        if (result <= 0)
        {
            DestroyWindow(window);
            if (result == 0)
                PostQuitMessage(static_cast<int>(message.wParam));
            break;
        }
        if (message.message == WM_KEYDOWN && message.wParam == VK_ESCAPE)
            DestroyWindow(window);
        else if (!IsDialogMessageW(window, &message))
        {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    if (owner)
    {
        EnableWindow(owner, TRUE);
        SetForegroundWindow(owner);
    }
#else
    std::cout << notices;
#endif
}
