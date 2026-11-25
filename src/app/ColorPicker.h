#pragma once

#include <windows.h>

using ColorPicked = void (*)(HWND owner, COLORREF color);

void ShowColorPicker(HWND owner, COLORREF color, ColorPicked onApply);
void CloseColorPicker();
bool ProcessColorPickerMessage(MSG& message);
