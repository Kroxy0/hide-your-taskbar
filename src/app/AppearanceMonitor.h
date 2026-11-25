
#pragma once

#include "TaskbarSession.h"

inline constexpr UINT WindowChangedMessage = WM_APP + 2;

LONG64 CurrentStyle();
void PublishAppearance();
void PublishColorPreview();
void SetAppearanceSession(TaskbarStats* stats, HANDLE changed);
void SetMonitoredTrayWindow(HWND window);
void UpdateWindowHooks(bool enable);
