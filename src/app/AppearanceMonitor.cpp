#include "AppearanceMonitor.h"
#include "Preferences.h"
#include <cwchar>

namespace
{
HWND trayWindow{};
TaskbarStats* sessionStats{};
HANDLE changeEvent{};
HWINEVENTHOOK foregroundHook{};
HWINEVENTHOOK locationHook{};

bool IsScreenFilling(HWND window)
{
    DWORD process = 0;
    GetWindowThreadProcessId(window, &process);
    if (!window || process == GetCurrentProcessId() || window == GetShellWindow() || IsIconic(window))
        return false;
    wchar_t name[64]{};
    GetClassNameW(window, name, ARRAYSIZE(name));
    if (std::wcscmp(name, L"Shell_TrayWnd") == 0 || std::wcscmp(name, L"Shell_SecondaryTrayWnd") == 0 ||
        std::wcscmp(name, L"WorkerW") == 0)
        return false;
    if (IsZoomed(window))
        return true;

    RECT bounds{};
    MONITORINFO monitor{};
    monitor.cbSize = sizeof(monitor);
    return GetWindowRect(window, &bounds) &&
        GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &monitor) &&
        bounds.left <= monitor.rcMonitor.left && bounds.top <= monitor.rcMonitor.top &&
        bounds.right >= monitor.rcMonitor.right && bounds.bottom >= monitor.rcMonitor.bottom;
}

void CALLBACK ObserveWindow(HWINEVENTHOOK, DWORD event, HWND window,
                            LONG object, LONG child, DWORD, DWORD)
{
    if (GetAppearanceSettings().separate && trayWindow &&
        (event == EVENT_SYSTEM_FOREGROUND ||
         (window == GetForegroundWindow() && object == OBJID_WINDOW && child == CHILDID_SELF)))
        PostMessageW(trayWindow, WindowChangedMessage, 0, 0);
}
}

LONG64 CurrentStyle()
{
    const auto& settings = GetAppearanceSettings();
    const DWORD mode = settings.separate && IsScreenFilling(GetForegroundWindow())
        ? settings.full : settings.normal;
    return PackAppearance(static_cast<TaskbarMode>(mode), settings.color, settings.opacity);
}

namespace
{
void PublishStyle(LONG64 style)
{
    if (!sessionStats || !changeEvent)
        return;
    if (InterlockedExchange64(&sessionStats->appearance, style) != style)
        SetEvent(changeEvent);
}
}

void PublishAppearance()
{
    PublishStyle(CurrentStyle());
}

void PublishColorPreview()
{
    const auto& settings = GetAppearanceSettings();
    PublishStyle(PackAppearance(TaskbarMode::Color, settings.color, settings.opacity));
}

void SetAppearanceSession(TaskbarStats* stats, HANDLE changed)
{
    sessionStats = stats;
    changeEvent = changed;
}

void SetMonitoredTrayWindow(HWND window)
{
    trayWindow = window;
}

void UpdateWindowHooks(bool enable)
{
    if (enable)
    {
        if (!foregroundHook)
            foregroundHook = SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND,
                nullptr, ObserveWindow, 0, 0, WINEVENT_OUTOFCONTEXT);
        if (!locationHook)
            locationHook = SetWinEventHook(EVENT_OBJECT_LOCATIONCHANGE, EVENT_OBJECT_LOCATIONCHANGE,
                nullptr, ObserveWindow, 0, 0, WINEVENT_OUTOFCONTEXT);
    }
    else
    {
        if (foregroundHook) UnhookWinEvent(foregroundHook);
        if (locationHook) UnhookWinEvent(locationHook);
        foregroundHook = nullptr;
        locationHook = nullptr;
    }
}
