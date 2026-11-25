#include "SettingsWindow.h"
#include "SettingsTheme.h"
#include "SettingsControlIds.h"
#include "Preferences.h"
#include "AppearanceMonitor.h"
#include "ColorPicker.h"
#include <commctrl.h>
#include <cwchar>
#include <initializer_list>

namespace
{
HWND settingsWindow{};
bool advancedOpen{};
bool opacityNeedsSave{};
bool previewingColor{};

void UpdateOpacityLabel(HWND dialog)
{
    wchar_t label[8]{};
    swprintf_s(label, L"%lu%%", static_cast<unsigned long>(GetAppearanceSettings().opacity));
    SetDlgItemTextW(dialog, IDC_OPACITY_VALUE, label);
}

void EndOpacityEdit()
{
    if (opacityNeedsSave)
    {
        SaveAppearanceSettings();
        opacityNeedsSave = false;
    }
    if (previewingColor)
    {
        previewingColor = false;
        PublishAppearance();
    }
}

void RefreshSettings(HWND dialog)
{
    const auto& settings = GetAppearanceSettings();
    const bool fullVisible = advancedOpen && settings.separate;
    const auto mode = static_cast<TaskbarMode>(advancedOpen ? settings.full : settings.normal);
    const bool colorVisible = mode == TaskbarMode::Color && (!advancedOpen || fullVisible);
    constexpr const wchar_t* titles[]{L"Default", L"Transparent", L"Blur", L"Color"};
    constexpr const wchar_t* descriptions[]{L"Use the standard Windows taskbar appearance.",
        L"Remove the taskbar background and keep its icons visible.",
        L"Softly blur what is behind the taskbar.",
        L"Choose a taskbar color and adjust its opacity."};
    SetSettingsPage(dialog, advancedOpen);
    SetDlgItemTextW(dialog, IDC_TITLE, advancedOpen ? L"Advanced settings" : titles[settings.normal]);
    SetDlgItemTextW(dialog, IDC_MODE_DESCRIPTION, advancedOpen
        ? L"Set a separate appearance for maximized or full-screen windows." : descriptions[settings.normal]);
    const COLORREF color = settings.color;
    wchar_t colorLabel[40]{};
    swprintf_s(colorLabel, L"Choose color   #%02X%02X%02X", static_cast<unsigned>(GetRValue(color)),
        static_cast<unsigned>(GetGValue(color)), static_cast<unsigned>(GetBValue(color)));
    SetDlgItemTextW(dialog, IDC_COLOR, colorLabel);
    SetDlgItemTextW(dialog, IDC_FULL_ENABLED, settings.separate
        ? L"Maximized or full screen   \u00b7   On" : L"Maximized or full screen   \u00b7   Off");
    SetDlgItemTextW(dialog, IDC_STARTUP, StartupEnabled()
        ? L"Start with Windows   \u00b7   On" : L"Start with Windows   \u00b7   Off");
    SendDlgItemMessageW(dialog, IDC_OPACITY, TBM_SETPOS, TRUE, settings.opacity);
    UpdateOpacityLabel(dialog);
    for (const int id : {IDC_COLOR, IDC_OPACITY_LABEL, IDC_OPACITY_VALUE, IDC_OPACITY})
        ShowWindow(GetDlgItem(dialog, id), colorVisible ? SW_SHOWNA : SW_HIDE);
    ShowWindow(GetDlgItem(dialog, IDC_MODE_PREVIEW), advancedOpen ? SW_HIDE : SW_SHOWNA);
    for (const int id : {IDC_FULL_ENABLED, IDC_STARTUP})
        ShowWindow(GetDlgItem(dialog, id), advancedOpen ? SW_SHOWNA : SW_HIDE);
    for (int id = IDC_FULL_DEFAULT; id <= IDC_FULL_COLOR; ++id)
        ShowWindow(GetDlgItem(dialog, id), fullVisible ? SW_SHOWNA : SW_HIDE);
    RedrawWindow(dialog, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
}

void ApplyPickedColor(HWND dialog, COLORREF color)
{
    GetAppearanceSettings().color = color;
    SaveAppearanceSettings();
    RefreshSettings(dialog);
    PublishAppearance();
}

INT_PTR CALLBACK SettingsProc(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == WM_TIMER && UpdateSettingsAnimation(dialog, wParam))
        return TRUE;
    if (message == WM_ERASEBKGND)
    {
        DrawSettingsBackground(dialog, reinterpret_cast<HDC>(wParam));
        return TRUE;
    }
    if (message == WM_INITDIALOG)
    {
        advancedOpen = false;
        InitializeSettingsTheme(dialog);
        SendDlgItemMessageW(dialog, IDC_OPACITY, TBM_SETRANGE, FALSE, MAKELPARAM(0, MaximumColorOpacity));
        SendDlgItemMessageW(dialog, IDC_OPACITY, TBM_SETPAGESIZE, 0, 10);
        RefreshSettings(dialog);
        return TRUE;
    }

    if (message == WM_HSCROLL && reinterpret_cast<HWND>(lParam) == GetDlgItem(dialog, IDC_OPACITY))
    {
        const auto position = static_cast<DWORD>(SendDlgItemMessageW(dialog, IDC_OPACITY, TBM_GETPOS, 0, 0));
        auto& settings = GetAppearanceSettings();
        if (settings.opacity != position)
        {
            settings.opacity = position;
            opacityNeedsSave = true;
            UpdateOpacityLabel(dialog);
            InvalidateRect(GetDlgItem(dialog, IDC_MODE_PREVIEW), nullptr, FALSE);
            previewingColor = settings.normal != static_cast<DWORD>(TaskbarMode::Color);
            if (previewingColor)
                PublishColorPreview();
            else
                PublishAppearance();
        }
        if (LOWORD(wParam) == TB_ENDTRACK)
            EndOpacityEdit();
        return TRUE;
    }
    if (message == WM_ACTIVATE && LOWORD(wParam) == WA_INACTIVE)
        EndOpacityEdit();
    if (message == WM_CLOSE || (message == WM_COMMAND && LOWORD(wParam) == IDCANCEL))
    {
        DestroyWindow(dialog);
        return TRUE;
    }
    if (message == WM_DESTROY)
    {
        CloseColorPicker();
        EndOpacityEdit();
        ReleaseSettingsTheme();
        settingsWindow = nullptr;
        return TRUE;
    }
    if (message != WM_COMMAND)
        return PaintSettingsControl(message, wParam, lParam);

    auto& settings = GetAppearanceSettings();
    const int id = LOWORD(wParam);
    if (HIWORD(wParam) != BN_CLICKED)
        return FALSE;
    EndOpacityEdit();
    if (id == IDC_ADVANCED)
    {
        advancedOpen = true;
        RefreshSettings(dialog);
        return TRUE;
    }
    if (IsModeButton(id, IDC_NORMAL_DEFAULT))
    {
        advancedOpen = false;
        settings.normal = static_cast<DWORD>(id - IDC_NORMAL_DEFAULT);
    }
    else if (IsModeButton(id, IDC_FULL_DEFAULT))
        settings.full = static_cast<DWORD>(id - IDC_FULL_DEFAULT);
    else if (id == IDC_FULL_ENABLED)
    {
        settings.separate = !settings.separate;
        UpdateWindowHooks(settings.separate != 0);
    }
    else if (id == IDC_STARTUP)
    {
        if (!SetStartup(!StartupEnabled()))
            MessageBoxW(dialog, L"Could not change the startup setting.", L"Hide Your Taskbar", MB_ICONWARNING);
    }
    else if (id == IDC_COLOR)
    {
        ShowColorPicker(dialog, settings.color, ApplyPickedColor);
        return TRUE;
    }
    else
        return FALSE;

    SaveAppearanceSettings();
    RefreshSettings(dialog);
    PublishAppearance();
    return TRUE;
}
}

void ShowSettings(HWND owner)
{
    if (!settingsWindow)
    {
        const INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_BAR_CLASSES};
        if (!InitCommonControlsEx(&controls))
            return;
        settingsWindow = CreateDialogParamW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDD_SETTINGS), owner, SettingsProc, 0);
    }
    if (settingsWindow)
    {
        RefreshSettings(settingsWindow);
        ShowWindow(settingsWindow, SW_SHOWNORMAL);
        SetForegroundWindow(settingsWindow);
    }
}

void CloseSettings()
{
    if (settingsWindow)
        DestroyWindow(settingsWindow);
}

bool ProcessSettingsMessage(MSG& message)
{
    return ProcessColorPickerMessage(message) || (settingsWindow && IsDialogMessageW(settingsWindow, &message));
}
