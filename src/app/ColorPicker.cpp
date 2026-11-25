#include "ColorPicker.h"

#include "ColorSpectrum.h"
#include "SettingsColors.h"
#include "SettingsControlIds.h"
#include "SettingsTheme.h"
#include <algorithm>
#include <commctrl.h>
#include <cwchar>
#include <memory>
#include <windowsx.h>

namespace
{
using namespace SettingsColors;

struct PickerState
{
    PickerState(HWND parent, COLORREF initial, ColorPicked apply)
        : owner(parent), onApply(apply), color(HsvColor::FromRgb(initial)) {}

    ~PickerState()
    {
        if (editBackground)
            DeleteObject(editBackground);
    }

    HWND owner;
    ColorPicked onApply;
    HsvColor color;
    HWND window{};
    HBRUSH editBackground{};
    bool updatingHex{};
    bool highContrast{};
    ColorSpectrum spectrum;
};

std::unique_ptr<PickerState> picker;

bool ParseHexColor(const wchar_t* text, COLORREF& color)
{
    if (*text == L'#')
        ++text;
    if (std::wcslen(text) != 6)
        return false;

    DWORD value = 0;
    for (; *text; ++text)
    {
        DWORD digit;
        if (*text >= L'0' && *text <= L'9')
            digit = *text - L'0';
        else if (*text >= L'A' && *text <= L'F')
            digit = *text - L'A' + 10;
        else if (*text >= L'a' && *text <= L'f')
            digit = *text - L'a' + 10;
        else
            return false;
        value = (value << 4) | digit;
    }
    color = RGB(value >> 16, value >> 8, value);
    return true;
}

void RedrawColor(PickerState& state)
{
    for (const int id : {IDC_COLOR_FIELD, IDC_COLOR_HUE, IDC_COLOR_PREVIEW})
        InvalidateRect(GetDlgItem(state.window, id), nullptr, FALSE);
}

void SetHexValidity(const PickerState& state, bool valid)
{
    EnableWindow(GetDlgItem(state.window, IDOK), valid);
    SetDlgItemTextW(state.window, IDC_COLOR_HINT, valid ? L"" : L"Enter a 6-digit HEX color, such as #285B88.");
}

void UpdateHexFromColor(PickerState& state)
{
    const COLORREF value = state.color.ToRgb();
    wchar_t hex[8]{};
    swprintf_s(hex, L"#%02X%02X%02X", static_cast<unsigned>(GetRValue(value)),
        static_cast<unsigned>(GetGValue(value)), static_cast<unsigned>(GetBValue(value)));
    state.updatingHex = true;
    SetDlgItemTextW(state.window, IDC_COLOR_HEX, hex);
    state.updatingHex = false;
    SetHexValidity(state, true);
    RedrawColor(state);
}

void PaintPalette(HWND control, HDC dc, PickerState& state, int id)
{
    const int saved = SaveDC(dc);
    if (!saved)
        return;

    RECT bounds{};
    GetClientRect(control, &bounds);
    const auto brush = static_cast<HBRUSH>(GetStockObject(DC_BRUSH));
    SetDCBrushColor(dc, state.highContrast ? GetSysColor(COLOR_WINDOW) : SettingsSurface);
    FillRect(dc, &bounds, brush);
    const int radius = MulDiv(12, GetDeviceCaps(dc, LOGPIXELSX), 96);
    POINT clipBounds[]{{bounds.left, bounds.top}, {bounds.right, bounds.bottom}};
    if (LPtoDP(dc, clipBounds, ARRAYSIZE(clipBounds)))
    {
        const HRGN corners = CreateRoundRectRgn(clipBounds[0].x, clipBounds[0].y,
            clipBounds[1].x, clipBounds[1].y, radius, radius);
        if (corners)
        {
            ExtSelectClipRgn(dc, corners, RGN_AND);
            DeleteObject(corners);
        }
    }
    if (id == IDC_COLOR_PREVIEW)
    {
        SetDCBrushColor(dc, state.color.ToRgb());
        FillRect(dc, &bounds, brush);
    }
    else
        state.spectrum.Draw(dc, bounds, state.color, id == IDC_COLOR_HUE, GetFocus() == control);
    RestoreDC(dc, saved);
}

void PickAtPoint(HWND control, LPARAM position, PickerState& state, bool hueOnly)
{
    RECT bounds{};
    GetClientRect(control, &bounds);
    if (bounds.right < 2 || bounds.bottom < 2)
        return;
    const double x = std::clamp(static_cast<double>(GET_X_LPARAM(position)) / (bounds.right - 1), 0.0, 1.0);
    if (hueOnly)
        state.color.hue = x * 360;
    else
    {
        const double y = std::clamp(static_cast<double>(GET_Y_LPARAM(position)) / (bounds.bottom - 1), 0.0, 1.0);
        state.color.saturation = x;
        state.color.value = 1 - y;
    }
    UpdateHexFromColor(state);
}

bool MoveSelection(PickerState& state, bool hueOnly, WPARAM key)
{
    if (key != VK_LEFT && key != VK_RIGHT && key != VK_UP && key != VK_DOWN)
        return false;

    if (hueOnly)
    {
        const double step = key == VK_LEFT || key == VK_DOWN ? -1 : 1;
        state.color.hue = std::clamp(state.color.hue + step, 0.0, 360.0);
    }
    else if (key == VK_LEFT || key == VK_RIGHT)
        state.color.saturation = std::clamp(state.color.saturation + (key == VK_LEFT ? -0.01 : 0.01), 0.0, 1.0);
    else
        state.color.value = std::clamp(state.color.value + (key == VK_DOWN ? -0.01 : 0.01), 0.0, 1.0);

    UpdateHexFromColor(state);
    return true;
}

LRESULT CALLBACK PaletteProc(HWND control, UINT message, WPARAM wParam, LPARAM lParam,
                             UINT_PTR id, DWORD_PTR data)
{
    auto& state = *reinterpret_cast<PickerState*>(data);
    const bool selectable = id != IDC_COLOR_PREVIEW;
    const bool hueOnly = id == IDC_COLOR_HUE;
    switch (message)
    {
    case WM_ERASEBKGND:
        return TRUE;
    case WM_PAINT:
    {
        PAINTSTRUCT paint{};
        const HDC dc = BeginPaint(control, &paint);
        PaintPalette(control, dc, state, static_cast<int>(id));
        EndPaint(control, &paint);
        return 0;
    }
    case WM_PRINTCLIENT:
        PaintPalette(control, reinterpret_cast<HDC>(wParam), state, static_cast<int>(id));
        return 0;
    case WM_LBUTTONDOWN:
        if (selectable)
        {
            SetFocus(control);
            SetCapture(control);
            PickAtPoint(control, lParam, state, hueOnly);
            return 0;
        }
        break;
    case WM_MOUSEMOVE:
        if (selectable && GetCapture() == control && (wParam & MK_LBUTTON))
            PickAtPoint(control, lParam, state, hueOnly);
        break;
    case WM_LBUTTONUP:
        if (selectable && GetCapture() == control)
        {
            PickAtPoint(control, lParam, state, hueOnly);
            ReleaseCapture();
            return 0;
        }
        break;
    case WM_GETDLGCODE:
        if (selectable)
            return DLGC_WANTARROWS;
        break;
    case WM_KEYDOWN:
        if (selectable && MoveSelection(state, hueOnly, wParam))
            return 0;
        break;
    case WM_SETFOCUS:
    case WM_KILLFOCUS:
        InvalidateRect(control, nullptr, FALSE);
        break;
    case WM_NCDESTROY:
        RemoveWindowSubclass(control, PaletteProc, id);
        break;
    }
    return DefSubclassProc(control, message, wParam, lParam);
}

INT_PTR CALLBACK PickerProc(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == WM_INITDIALOG)
    {
        auto& state = *reinterpret_cast<PickerState*>(lParam);
        state.window = dialog;
        state.highContrast = IsHighContrast();
        if (!state.highContrast)
            state.editBackground = CreateSolidBrush(CardSurface);
        SetWindowLongPtrW(dialog, DWLP_USER, lParam);
        StyleSettingsCaption(dialog);
        SendDlgItemMessageW(dialog, IDC_COLOR_HEX, EM_SETLIMITTEXT, 7, 0);
        SendDlgItemMessageW(dialog, IDC_COLOR_HEX, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(8, 8));
        for (const int id : {IDC_COLOR_FIELD, IDC_COLOR_HUE, IDC_COLOR_PREVIEW})
            SetWindowSubclass(GetDlgItem(dialog, id), PaletteProc, id, reinterpret_cast<DWORD_PTR>(&state));
        UpdateHexFromColor(state);
        return TRUE;
    }
    auto* state = reinterpret_cast<PickerState*>(GetWindowLongPtrW(dialog, DWLP_USER));
    if (!state)
        return FALSE;
    if (message == WM_COMMAND && LOWORD(wParam) == IDC_COLOR_HEX && HIWORD(wParam) == EN_CHANGE)
    {
        if (!state->updatingHex)
        {
            wchar_t hex[8]{};
            GetDlgItemTextW(dialog, IDC_COLOR_HEX, hex, ARRAYSIZE(hex));
            COLORREF color{};
            const bool valid = ParseHexColor(hex, color);
            SetHexValidity(*state, valid);
            if (valid)
            {
                state->color = HsvColor::FromRgb(color, state->color.hue);
                RedrawColor(*state);
            }
        }
        return TRUE;
    }
    if (message == WM_COMMAND && LOWORD(wParam) == IDOK && IsWindowEnabled(GetDlgItem(dialog, IDOK)))
    {
        const COLORREF color = state->color.ToRgb();
        const HWND owner = state->owner;
        const auto apply = state->onApply;
        CloseColorPicker();
        if (IsWindow(owner))
            apply(owner, color);
        return TRUE;
    }
    if (message == WM_CLOSE || (message == WM_COMMAND && LOWORD(wParam) == IDCANCEL))
    {
        CloseColorPicker();
        return TRUE;
    }
    if (message == WM_NCDESTROY)
    {
        if (picker.get() == state)
            picker.reset();
        return FALSE;
    }
    if (message == WM_CTLCOLOREDIT && state->editBackground)
    {
        const auto dc = reinterpret_cast<HDC>(wParam);
        SetBkColor(dc, CardSurface);
        SetTextColor(dc, TextColor);
        return reinterpret_cast<INT_PTR>(state->editBackground);
    }
    return PaintSettingsControl(message, wParam, lParam);
}
}

void ShowColorPicker(HWND owner, COLORREF color, ColorPicked onApply)
{
    if (!picker)
    {
        picker = std::make_unique<PickerState>(owner, color, onApply);
        const HWND dialog = CreateDialogParamW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDD_COLOR_PICKER),
            owner, PickerProc, reinterpret_cast<LPARAM>(picker.get()));
        if (!dialog)
        {
            picker.reset();
            return;
        }
        EnableWindow(owner, FALSE);
        ShowWindow(dialog, SW_SHOWNORMAL);
        SetFocus(GetDlgItem(dialog, IDC_COLOR_HEX));
        SendDlgItemMessageW(dialog, IDC_COLOR_HEX, EM_SETSEL, 0, -1);
    }
    SetForegroundWindow(picker->window);
}

void CloseColorPicker()
{
    if (!picker)
        return;
    const HWND dialog = picker->window;
    EnableWindow(picker->owner, TRUE);
    DestroyWindow(dialog);
}

bool ProcessColorPickerMessage(MSG& message)
{
    if (!picker || (message.hwnd != picker->window && !IsChild(picker->window, message.hwnd)))
        return false;
    if (message.message == WM_KEYDOWN && message.wParam == VK_RETURN &&
        GetDlgCtrlID(message.hwnd) != IDCANCEL)
    {
        SendMessageW(picker->window, WM_COMMAND, IDOK, 0);
        return true;
    }
    return IsDialogMessageW(picker->window, &message);
}
