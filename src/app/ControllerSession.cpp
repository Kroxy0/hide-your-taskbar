#include "ControllerSession.h"
#include "AppearanceMonitor.h"
#include "Preferences.h"
#include "Tray.h"
#include <xamlom.h>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <winrt/base.h>

namespace
{
HRESULT AttachTaskbar()
{
    DWORD explorer = 0;
    GetWindowThreadProcessId(FindWindowW(L"Shell_TrayWnd", nullptr), &explorer);
    if (!explorer)
        return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);

    wchar_t executable[32768]{};
    const DWORD length = GetModuleFileNameW(nullptr, executable, ARRAYSIZE(executable));
    if (!length || length == ARRAYSIZE(executable))
        return HRESULT_FROM_WIN32(GetLastError());
    const auto library = std::filesystem::path(executable).replace_filename(L"TaskbarAppearance.dll");

    const HMODULE xaml = LoadLibraryExW(L"Windows.UI.Xaml.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!xaml)
        return HRESULT_FROM_WIN32(GetLastError());

    using Initialize = decltype(&InitializeXamlDiagnosticsEx);
    const auto initialize = reinterpret_cast<Initialize>(GetProcAddress(xaml, "InitializeXamlDiagnosticsEx"));
    HRESULT result = HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND);
    if (initialize)
    {
        for (int index = 1; index <= 64; ++index)
        {
            const auto endpoint = L"VisualDiagConnection" + std::to_wstring(index);
            result = initialize(endpoint.c_str(), explorer, nullptr, library.c_str(), TaskbarSiteId, nullptr);
            if (result != HRESULT_FROM_WIN32(ERROR_NOT_FOUND) &&
                result != HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND))
                break;
        }
    }
    FreeLibrary(xaml);
    return result;
}
}

int RunSession(bool test, bool waitForExplorer)
{
    const winrt::handle stop(CreateEventW(nullptr, TRUE, FALSE, TaskbarStop));
    const winrt::handle done(CreateEventW(nullptr, TRUE, FALSE, TaskbarDone));
    const winrt::handle ready(CreateEventW(nullptr, TRUE, FALSE, TaskbarReady));
    const winrt::handle applied(CreateEventW(nullptr, TRUE, FALSE, TaskbarApplied));
    const winrt::handle changed(CreateEventW(nullptr, FALSE, FALSE, TaskbarChanged));
    const winrt::handle updated(CreateEventW(nullptr, FALSE, FALSE, TaskbarUpdated));
    const winrt::handle mapping(CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
        0, sizeof(TaskbarStats), TaskbarStatsName));
    if (!stop || !done || !ready || !applied || !changed || !updated || !mapping)
        return 1;
    for (const HANDLE event : {stop.get(), done.get(), ready.get(), applied.get(), changed.get(), updated.get()})
        ResetEvent(event);
    const std::unique_ptr<TaskbarStats, UnmapTaskbarStats> stats(static_cast<TaskbarStats*>(
        MapViewOfFile(mapping.get(), FILE_MAP_WRITE, 0, 0, sizeof(TaskbarStats))));
    if (!stats)
        return 1;
    *stats = {};
    LoadAppearanceSettings();
    stats->appearance = test ? PackAppearance(TaskbarMode::Clear) : CurrentStyle();

    HRESULT result = AttachTaskbar();
    for (int attempt = 0; waitForExplorer && FAILED(result) && attempt < 10; ++attempt)
    {
        Sleep(1000);
        result = AttachTaskbar();
    }
    if (FAILED(result))
    {
        std::fwprintf(stderr, L"Could not connect to taskbar XAML (0x%08lX).\n", static_cast<unsigned long>(result));
        return 1;
    }

    const HANDLE signals[]{ready.get(), done.get()};
    int status = 1;
    if (WaitForMultipleObjects(ARRAYSIZE(signals), signals, FALSE, 5000) == WAIT_OBJECT_0 &&
        WaitForSingleObject(applied.get(), 3000) == WAIT_OBJECT_0)
    {
        std::printf("Taskbar elements styled: %ld\n", InterlockedCompareExchange(&stats->applied, 0, 0));
        if (test)
        {
            status = 0;
            constexpr COLORREF testColor = RGB(40, 91, 136);
            for (const LONG64 style : {PackAppearance(TaskbarMode::Color, testColor, 0),
                     PackAppearance(TaskbarMode::Color, testColor, 50),
                     PackAppearance(TaskbarMode::Color, testColor, MaximumColorOpacity),
                     PackAppearance(TaskbarMode::Blur), PackAppearance(TaskbarMode::Default), PackAppearance(TaskbarMode::Clear)})
            {
                InterlockedExchange64(&stats->appearance, style);
                SetEvent(changed.get());
                if (WaitForSingleObject(updated.get(), 4000) != WAIT_OBJECT_0)
                {
                    status = 1;
                    break;
                }
            }
        }
        else
        {
            SetAppearanceSession(stats.get(), changed.get());
            status = RunTray(stop.get());
            SetAppearanceSession(nullptr, nullptr);
        }
    }
    else
        std::fputs("Could not find the taskbar's XAML background.\n", stderr);

    SetEvent(stop.get());
    if (status == 2)
        return status;
    if (WaitForSingleObject(done.get(), 5000) != WAIT_OBJECT_0)
    {
        std::fputs("Could not confirm that the XAML session closed.\n", stderr);
        status = 1;
    }
    else
    {
        const LONG restored = InterlockedCompareExchange(&stats->restored, 0, 0);
        const LONG errors = InterlockedCompareExchange(&stats->errors, 0, 0);
        std::printf("Taskbar elements restored: %ld, errors: %ld\n", restored, errors);
        if (errors != 0)
            status = 1;
    }
    return status;
}
