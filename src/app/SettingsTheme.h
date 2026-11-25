
#pragma once

#include <windows.h>

bool IsHighContrast();
bool IsModeButton(int id, int first);
void StyleSettingsCaption(HWND dialog);
void InitializeSettingsTheme(HWND dialog);
void DrawSettingsBackground(HWND dialog, HDC dc);
void SetSettingsPage(HWND dialog, bool advanced);
bool UpdateSettingsAnimation(HWND dialog, UINT_PTR timer);
void ReleaseSettingsTheme();
INT_PTR PaintSettingsControl(UINT message, WPARAM wParam, LPARAM lParam);
