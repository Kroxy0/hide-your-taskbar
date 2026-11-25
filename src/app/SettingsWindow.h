
#pragma once

#include <windows.h>

void ShowSettings(HWND owner);
void CloseSettings();
bool ProcessSettingsMessage(MSG& message);
