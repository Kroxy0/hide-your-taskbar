
#pragma once

#include "TaskbarSession.h"

struct AppearanceSettings
{
    DWORD normal = static_cast<DWORD>(TaskbarMode::Clear);
    DWORD full = static_cast<DWORD>(TaskbarMode::Default);
    DWORD separate = 0;
    DWORD color = RGB(40, 91, 136);
    DWORD opacity = DefaultColorOpacity;
};

AppearanceSettings& GetAppearanceSettings();
void LoadAppearanceSettings();
void SaveAppearanceSettings();
bool StartupEnabled();
bool SetStartup(bool enable);
