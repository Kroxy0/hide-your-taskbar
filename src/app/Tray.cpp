#include "Tray.h"
#include "AppearanceMonitor.h"
#include "Preferences.h"
#include "SettingsTheme.h"
#include "SettingsWindow.h"
#include "SettingsControlIds.h"
#include <shellapi.h>
#include <cwchar>

namespace
{
constexpr UINT TrayMessage = WM_APP + 1;
constexpr UINT ExitCommand = 1;
constexpr UINT StartupCommand = 2;
constexpr UINT SettingsCommand = 3;
const UINT TaskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");

void ApplyTrayMenuTheme()
{
    enum class PreferredAppMode { Default = 0, ForceDark = 2 };
    using SetPreferredAppMode = PreferredAppMode(WINAPI*)(PreferredAppMode);
    using FlushMenuThemes = void(WINAPI*)();

    // Native Win11 menu theming is exposed through these ordinal exports.
    static const HMODULE theme = LoadLibraryExW(L"uxtheme.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!theme)
        return;

    static const auto setMode = reinterpret_cast<SetPreferredAppMode>(
        GetProcAddress(theme, MAKEINTRESOURCEA(135)));
    static const auto flush = reinterpret_cast<FlushMenuThemes>(
        GetProcAddress(theme, MAKEINTRESOURCEA(136)));
    if (!setMode || !flush)
        return;

    const auto mode = IsHighContrast() ? PreferredAppMode::Default : PreferredAppMode::ForceDark;
    if (setMode(mode) != mode)
        flush();
}

NOTIFYICONDATAW TrayIcon(HWND window)
{
    NOTIFYICONDATAW icon{};
    icon.cbSize = sizeof(icon);
    icon.hWnd = window;
    icon.uID = 1;
    icon.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    icon.uCallbackMessage = TrayMessage;
    icon.hIcon = static_cast<HICON>(LoadImageW(GetModuleHandleW(nullptr),
        MAKEINTRESOURCEW(IDI_TASKBAR), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON),
        GetSystemMetrics(SM_CYSMICON), LR_SHARED));
    wcscpy_s(icon.szTip, L"Hide Your Taskbar");
    return icon;
}

LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == WindowChangedMessage)
    {
        PublishAppearance();
        return 0;
    }
    if (message == TrayMessage && lParam == WM_LBUTTONDBLCLK)
    {
        ShowSettings(window);
        return 0;
    }
    if (message == TaskbarCreated)
    {
        PostQuitMessage(2);
        return 0;
    }

    if (message == TrayMessage && (lParam == WM_RBUTTONUP || lParam == WM_CONTEXTMENU))
    {
        ApplyTrayMenuTheme();
        POINT position{};
        GetCursorPos(&position);
        if (const HMENU menu = CreatePopupMenu())
        {
            const bool startup = StartupEnabled();
            AppendMenuW(menu, MF_STRING, SettingsCommand, L"Settings");
            SetMenuDefaultItem(menu, SettingsCommand, FALSE);
            AppendMenuW(menu, MF_STRING | (startup ? MF_CHECKED : MF_UNCHECKED),
                StartupCommand, L"Start with Windows");
            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(menu, MF_STRING, ExitCommand, L"Exit");
            SetForegroundWindow(window);
            const UINT command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON,
                position.x, position.y, 0, window, nullptr);
            DestroyMenu(menu);
            if (command == ExitCommand)
                PostQuitMessage(0);
            else if (command == SettingsCommand)
                ShowSettings(window);
            else if (command == StartupCommand && !SetStartup(!startup))
                MessageBoxW(window, L"Could not change the startup setting.", L"Hide Your Taskbar", MB_ICONWARNING);
            PostMessageW(window, WM_NULL, 0, 0);
        }
        return 0;
    }
    if (message == WM_CLOSE)
    {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}
}

int RunTray(HANDLE stop)
{
    const HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSW cls{};
    cls.hInstance = instance;
    cls.lpfnWndProc = WindowProc;
    cls.lpszClassName = L"HideYourTaskbar.Controller";
    if (!RegisterClassW(&cls) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        return 1;

    const HWND window = CreateWindowExW(0, cls.lpszClassName, L"Hide Your Taskbar",
        WS_OVERLAPPED, 0, 0, 0, 0, nullptr, nullptr, instance, nullptr);
    if (!window)
        return 1;
    auto icon = TrayIcon(window);
    if (!Shell_NotifyIconW(NIM_ADD, &icon))
    {
        DestroyWindow(window);
        return 1;
    }
    SetMonitoredTrayWindow(window);
    UpdateWindowHooks(GetAppearanceSettings().separate != 0);
    PublishAppearance();

    MSG message{};
    int status = 0;
    for (;;)
    {
        const DWORD result = MsgWaitForMultipleObjectsEx(1, &stop, INFINITE, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        if (result == WAIT_OBJECT_0)
            break;
        if (result != WAIT_OBJECT_0 + 1)
        {
            status = 1;
            break;
        }
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
        {
            if (message.message == WM_QUIT)
            {
                status = static_cast<int>(message.wParam);
                break;
            }
            if (!ProcessSettingsMessage(message))
            {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
        }
        if (message.message == WM_QUIT)
            break;
    }

    SetMonitoredTrayWindow(nullptr);
    UpdateWindowHooks(false);
    CloseSettings();
    Shell_NotifyIconW(NIM_DELETE, &icon);
    DestroyWindow(window);
    return status;
}
