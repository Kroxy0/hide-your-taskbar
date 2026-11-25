#pragma once

#include <windows.h>

inline constexpr CLSID TaskbarSiteId =
    {0xb80f1ac7, 0x2b47, 0x4085, {0xa8, 0x47, 0x12, 0x38, 0xea, 0x61, 0xc7, 0xa3}};
inline constexpr wchar_t TaskbarMutex[] = L"Local\\HideYourTaskbar.Instance";
inline constexpr wchar_t TaskbarStop[] = L"Local\\HideYourTaskbar.Stop";
inline constexpr wchar_t TaskbarDone[] = L"Local\\HideYourTaskbar.Done";
inline constexpr wchar_t TaskbarReady[] = L"Local\\HideYourTaskbar.Ready";
inline constexpr wchar_t TaskbarApplied[] = L"Local\\HideYourTaskbar.Applied";
inline constexpr wchar_t TaskbarStatsName[] = L"Local\\HideYourTaskbar.AppearanceStats";
inline constexpr wchar_t TaskbarChanged[] = L"Local\\HideYourTaskbar.AppearanceChanged";
inline constexpr wchar_t TaskbarUpdated[] = L"Local\\HideYourTaskbar.AppearanceUpdated";

enum class TaskbarMode : DWORD { Default, Clear, Blur, Color };
inline constexpr DWORD DefaultColorOpacity = 86;
inline constexpr DWORD MaximumColorOpacity = 100;

inline constexpr LONG64 PackAppearance(TaskbarMode mode, COLORREF color = 0, DWORD opacity = DefaultColorOpacity)
{
    return (static_cast<LONG64>(opacity) << 32) | (static_cast<LONG64>(mode) << 24) | (color & 0x00ffffff);
}

inline constexpr TaskbarMode AppearanceMode(LONG64 value)
{
    return static_cast<TaskbarMode>((value >> 24) & 0xff);
}

inline constexpr BYTE AppearanceAlpha(LONG64 value)
{
    const auto opacity = static_cast<DWORD>(value >> 32);
    return static_cast<BYTE>((opacity * 255 + MaximumColorOpacity / 2) / MaximumColorOpacity);
}

struct TaskbarStats
{
    LONG applied;
    LONG restored;
    LONG errors;
    alignas(8) LONG64 appearance;
};

struct UnmapTaskbarStats
{
    void operator()(TaskbarStats* stats) const noexcept { UnmapViewOfFile(stats); }
};
