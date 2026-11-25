#include "SettingsTheme.h"
#include "SettingsColors.h"
#include "Preferences.h"
#include "SettingsControlIds.h"
#include <commctrl.h>
#include <dwmapi.h>
#include <cwchar>
#include <array>

namespace
{
using namespace SettingsColors;
constexpr COLORREF ModeAccents[]{RGB(160, 185, 224), RGB(94, 230, 194), RGB(193, 160, 255), ColorAccent};

HBRUSH settingsBackground{};
HFONT settingsTitle{};
HFONT modeLabelFont{};
bool settingsHighContrast{};
constexpr UINT_PTR PageAnimationTimer = 1;
constexpr ULONGLONG PageAnimationDuration = 180;
constexpr int AdvancedPageIndex = 4;
constexpr COLORREF NavigationAccent = RGB(204, 169, 255);
int selectedPage{-1};
std::array<unsigned, 5> previousSelection{};
ULONGLONG transitionStarted{};

COLORREF BlendColor(COLORREF base, COLORREF accent, unsigned strength)
{
    const auto mix = [strength](unsigned a, unsigned b) { return (a * (100 - strength) + b * strength) / 100; };
    return RGB(mix(GetRValue(base), GetRValue(accent)), mix(GetGValue(base), GetGValue(accent)),
        mix(GetBValue(base), GetBValue(accent)));
}

unsigned TransitionProgress()
{
    if (!transitionStarted)
        return 100;
    const ULONGLONG elapsed = GetTickCount64() - transitionStarted;
    if (elapsed >= PageAnimationDuration)
        return 100;
    const double remaining = 1.0 - static_cast<double>(elapsed) / PageAnimationDuration;
    return static_cast<unsigned>(100 * (1 - remaining * remaining * remaining));
}

unsigned SelectionStrength(int page, unsigned progress)
{
    const unsigned target = page == selectedPage ? 100 : 0;
    return (previousSelection[page] * (100 - progress) + target * progress) / 100;
}

struct PreviewColors
{
    COLORREF left;
    COLORREF right;
};

constexpr PreviewColors PreviewBackground{RGB(32, 56, 105), RGB(87, 48, 128)};
PreviewColors previousPreview = PreviewBackground;

PreviewColors TaskbarPreviewColors(int page)
{
    const auto& settings = GetAppearanceSettings();
    if (page == static_cast<int>(TaskbarMode::Color))
        return {BlendColor(PreviewBackground.left, settings.color, settings.opacity),
            BlendColor(PreviewBackground.right, settings.color, settings.opacity)};
    if (page == static_cast<int>(TaskbarMode::Default))
        return {SettingsSurface, CardSurface};
    if (page == static_cast<int>(TaskbarMode::Blur))
        return {RGB(34, 43, 69), RGB(55, 35, 82)};
    return PreviewBackground;
}

PreviewColors DisplayedPreviewColors(unsigned progress)
{
    const auto target = TaskbarPreviewColors(selectedPage);
    return {BlendColor(previousPreview.left, target.left, progress),
        BlendColor(previousPreview.right, target.right, progress)};
}

void DrawKeyboardFocus(HWND control, HDC dc, const RECT& bounds)
{
    if (SendMessageW(control, WM_QUERYUISTATE, 0, 0) & UISF_HIDEFOCUS)
        return;

    const int previous = SaveDC(dc);
    if (!previous)
        return;
    const RECT focus{bounds.left + 14, bounds.bottom - 6, bounds.left + 34, bounds.bottom - 4};
    SetDCBrushColor(dc, settingsHighContrast ? GetSysColor(COLOR_BTNTEXT) : NavigationAccent);
    FillRect(dc, &focus, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
    RestoreDC(dc, previous);
}

void FillGradient(HDC dc, const RECT& bounds, COLORREF from, COLORREF to, ULONG direction)
{
    const auto vertex = [](LONG x, LONG y, COLORREF color)
    {
        return TRIVERTEX{x, y, static_cast<COLOR16>(GetRValue(color) << 8),
            static_cast<COLOR16>(GetGValue(color) << 8), static_cast<COLOR16>(GetBValue(color) << 8), 0};
    };
    TRIVERTEX corners[]{vertex(bounds.left, bounds.top, from), vertex(bounds.right, bounds.bottom, to)};
    GRADIENT_RECT gradient{0, 1};
    GradientFill(dc, corners, ARRAYSIZE(corners), &gradient, 1, direction);
}

void DrawControlBackdrop(HWND control, HDC dc, const RECT& bounds)
{
    const HWND parent = GetParent(control);
    if (settingsHighContrast || !GetDlgItem(parent, IDC_TITLE))
    {
        FillRect(dc, &bounds, settingsBackground);
        return;
    }
    POINT origin{};
    MapWindowPoints(control, parent, &origin, 1);
    const int saved = SaveDC(dc);
    if (!saved)
        return;
    IntersectClipRect(dc, bounds.left, bounds.top, bounds.right, bounds.bottom);
    OffsetViewportOrgEx(dc, -origin.x, -origin.y, nullptr);
    DrawSettingsBackground(parent, dc);
    RestoreDC(dc, saved);
}

void DrawModePreview(const DRAWITEMSTRUCT& item)
{
    const HDC dc = item.hDC;
    const int saved = SaveDC(dc);
    if (!saved) return;
    const RECT bounds = item.rcItem;
    if (settingsHighContrast)
        FillRect(dc, &bounds, GetSysColorBrush(COLOR_WINDOW));
    else
        FillGradient(dc, bounds, PreviewBackground.left, PreviewBackground.right, GRADIENT_FILL_RECT_H);
    RECT bar = bounds;
    const LONG height = bounds.bottom - bounds.top;
    bar.top = bar.bottom - height / 4;
    if (settingsHighContrast)
        FillRect(dc, &bar, GetSysColorBrush(COLOR_BTNFACE));
    else
    {
        const auto colors = DisplayedPreviewColors(TransitionProgress());
        FillGradient(dc, bar, colors.left, colors.right, GRADIENT_FILL_RECT_H);
    }
    SelectObject(dc, GetStockObject(DC_BRUSH));
    SelectObject(dc, GetStockObject(NULL_PEN));
    SetDCBrushColor(dc, settingsHighContrast ? GetSysColor(COLOR_BTNTEXT) : TextColor);
    const LONG size = height / 10;
    const LONG center = (bar.left + bar.right) / 2;
    const LONG top = bar.top + (bar.bottom - bar.top - size) / 2;
    for (int i = -2; i <= 2; ++i)
    {
        const LONG left = center + i * (size + 6) - size / 2;
        RoundRect(dc, left, top, left + size, top + size, 4, 4);
    }
    RestoreDC(dc, saved);
}

void DrawSettingsButton(const DRAWITEMSTRUCT& item)
{
    const int savedDc = SaveDC(item.hDC);
    const int id = static_cast<int>(item.CtlID);
    const bool normal = IsModeButton(id, IDC_NORMAL_DEFAULT);
    const bool full = IsModeButton(id, IDC_FULL_DEFAULT);
    const int mode = normal ? id - IDC_NORMAL_DEFAULT : full ? id - IDC_FULL_DEFAULT : 0;
    const auto& settings = GetAppearanceSettings();
    const bool navigation = normal || id == IDC_ADVANCED;
    const int page = id == IDC_ADVANCED ? AdvancedPageIndex : mode;
    const bool selected = navigation ? selectedPage == page : full && settings.full == static_cast<DWORD>(mode);
    const unsigned emphasis = navigation ? SelectionStrength(page, TransitionProgress()) : selected ? 100 : 0;
    const bool primary = id == IDOK;
    const COLORREF accent = navigation ? NavigationAccent : primary ? ColorAccent : ModeAccents[mode];
    const bool pressed = (item.itemState & ODS_SELECTED) != 0;
    const COLORREF fill = settingsHighContrast ? GetSysColor(selected ? COLOR_HIGHLIGHT : COLOR_BTNFACE)
        : primary && !(item.itemState & ODS_DISABLED) ? (pressed ? BlendColor(accent, SettingsSurface, 15) : accent)
        : BlendColor(CardSurface, accent, pressed ? 20 : emphasis * 13 / 100);
    const COLORREF line = settingsHighContrast ? GetSysColor(COLOR_BTNTEXT)
        : BlendColor(CardEdge, accent, emphasis);
    DrawControlBackdrop(item.hwndItem, item.hDC, item.rcItem);
    SetDCBrushColor(item.hDC, fill);
    SetDCPenColor(item.hDC, line);
    const auto oldBrush = SelectObject(item.hDC, GetStockObject(DC_BRUSH));
    const auto oldPen = SelectObject(item.hDC, GetStockObject(DC_PEN));
    const RECT bounds = item.rcItem;
    const int dpi = GetDeviceCaps(item.hDC, LOGPIXELSX);
    const int radius = MulDiv(14, dpi, 96);
    RoundRect(item.hDC, bounds.left, bounds.top, bounds.right, bounds.bottom, radius, radius);
    SelectObject(item.hDC, oldPen);
    SelectObject(item.hDC, oldBrush);

    wchar_t caption[96]{};
    GetWindowTextW(item.hwndItem, caption, ARRAYSIZE(caption));
    if (const auto font = normal && modeLabelFont ? modeLabelFont
        : reinterpret_cast<HFONT>(SendMessageW(item.hwndItem, WM_GETFONT, 0, 0)))
        SelectObject(item.hDC, font);
    SetBkMode(item.hDC, TRANSPARENT);
    const bool disabled = (item.itemState & ODS_DISABLED) != 0;
    SetTextColor(item.hDC, settingsHighContrast ? GetSysColor(disabled ? COLOR_GRAYTEXT : selected ? COLOR_HIGHLIGHTTEXT : COLOR_BTNTEXT)
        : disabled ? SubtleText : primary ? SettingsSurface : BlendColor(TextColor, accent, emphasis));
    RECT textBounds = bounds;
    InflateRect(&textBounds, -8, 0);
    if (navigation)
        textBounds.left += MulDiv(9, dpi, 96);
    if (id == IDC_COLOR && !settingsHighContrast)
    {
        const LONG size = MulDiv(15, dpi, 96);
        const LONG left = bounds.left + MulDiv(12, dpi, 96);
        const LONG top = bounds.top + (bounds.bottom - bounds.top - size) / 2;
        const auto swatch = SelectObject(item.hDC, GetStockObject(DC_BRUSH));
        const auto border = SelectObject(item.hDC, GetStockObject(DC_PEN));
        SetDCBrushColor(item.hDC, settings.color);
        SetDCPenColor(item.hDC, SubtleText);
        RoundRect(item.hDC, left, top, left + size, top + size, 5, 5);
        SelectObject(item.hDC, border);
        SelectObject(item.hDC, swatch);
        textBounds.left = left + size + MulDiv(6, dpi, 96);
    }
    DrawTextW(item.hDC, caption, -1, &textBounds,
        (navigation ? DT_LEFT : DT_CENTER) | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    if ((item.itemState & ODS_FOCUS) && !(item.itemState & ODS_NOFOCUSRECT))
        DrawKeyboardFocus(item.hwndItem, item.hDC, bounds);
    if (savedDc)
        RestoreDC(item.hDC, savedDc);
}

LRESULT DrawOpacitySlider(const NMCUSTOMDRAW& draw)
{
    if (settingsHighContrast || draw.dwDrawStage != CDDS_PREPAINT)
        return CDRF_DODEFAULT;

    const HWND slider = draw.hdr.hwndFrom;
    const HDC dc = draw.hdc;
    const int saved = SaveDC(dc);
    if (!saved)
        return CDRF_DODEFAULT;

    RECT bounds{}, channel{}, thumb{};
    GetClientRect(slider, &bounds);
    SendMessageW(slider, TBM_GETCHANNELRECT, 0, reinterpret_cast<LPARAM>(&channel));
    SendMessageW(slider, TBM_GETTHUMBRECT, 0, reinterpret_cast<LPARAM>(&thumb));
    DrawControlBackdrop(slider, dc, bounds);

    const int dpi = GetDeviceCaps(dc, LOGPIXELSX);
    const int trackRadius = MulDiv(3, dpi, 96);
    const int thumbRadius = MulDiv(7, dpi, 96);
    const LONG centerX = (thumb.left + thumb.right) / 2;
    const LONG centerY = (thumb.top + thumb.bottom) / 2;
    SelectObject(dc, GetStockObject(DC_BRUSH));
    SelectObject(dc, GetStockObject(NULL_PEN));
    SetDCBrushColor(dc, CardEdge);
    RoundRect(dc, channel.left, centerY - trackRadius, channel.right, centerY + trackRadius, trackRadius, trackRadius);
    SetDCBrushColor(dc, ColorAccent);
    if (centerX > channel.left)
        RoundRect(dc, channel.left, centerY - trackRadius, centerX, centerY + trackRadius, trackRadius, trackRadius);
    Ellipse(dc, centerX - thumbRadius, centerY - thumbRadius, centerX + thumbRadius, centerY + thumbRadius);
    if (GetFocus() == slider)
        DrawKeyboardFocus(slider, dc, bounds);
    RestoreDC(dc, saved);
    return CDRF_SKIPDEFAULT;
}

void DrawOpacityValue(const DRAWITEMSTRUCT& item)
{
    const HDC dc = item.hDC;
    const int saved = SaveDC(dc);
    if (!saved)
        return;
    DrawControlBackdrop(item.hwndItem, dc, item.rcItem);
    const auto font = reinterpret_cast<HFONT>(SendMessageW(item.hwndItem, WM_GETFONT, 0, 0));
    if (font)
        SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, settingsHighContrast ? GetSysColor(COLOR_WINDOWTEXT) : ColorAccent);
    wchar_t text[8]{};
    GetWindowTextW(item.hwndItem, text, ARRAYSIZE(text));
    RECT bounds = item.rcItem;
    DrawTextW(dc, text, -1, &bounds, DT_RIGHT | DT_SINGLELINE | DT_NOPREFIX);
    RestoreDC(dc, saved);
}
}

using namespace SettingsColors;

void DrawSettingsBackground(HWND dialog, HDC dc)
{
    RECT bounds{};
    GetClientRect(dialog, &bounds);
    if (settingsHighContrast)
    {
        FillRect(dc, &bounds, GetSysColorBrush(COLOR_WINDOW));
        return;
    }
    RECT divider{0, 0, 127, 0};
    MapDialogRect(dialog, &divider);
    const HBRUSH brush = static_cast<HBRUSH>(GetStockObject(DC_BRUSH));
    SetDCBrushColor(dc, RGB(21, 18, 39));
    const RECT sidebar{0, 0, divider.right, bounds.bottom};
    FillRect(dc, &sidebar, brush);
    bounds.left = divider.right;
    FillGradient(dc, bounds, RGB(20, 25, 47), RGB(59, 29, 82), GRADIENT_FILL_RECT_H);
}

void SetSettingsPage(HWND dialog, bool advanced)
{
    const int next = advanced ? AdvancedPageIndex : static_cast<int>(GetAppearanceSettings().normal);
    if (selectedPage == next)
        return;
    const int last = selectedPage;
    const unsigned progress = TransitionProgress();
    previousPreview = DisplayedPreviewColors(progress);
    for (int page = 0; page <= AdvancedPageIndex; ++page)
        previousSelection[page] = SelectionStrength(page, progress);
    selectedPage = next;
    transitionStarted = 0;
    BOOL enabled = FALSE;
    if (last >= 0 && !settingsHighContrast &&
        SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &enabled, 0) && enabled &&
        SetTimer(dialog, PageAnimationTimer, 15, nullptr))
        transitionStarted = GetTickCount64();
}

bool UpdateSettingsAnimation(HWND dialog, UINT_PTR timer)
{
    if (timer != PageAnimationTimer)
        return false;
    if (TransitionProgress() == 100)
    {
        KillTimer(dialog, timer);
        transitionStarted = 0;
    }
    for (int id = IDC_NORMAL_DEFAULT; id <= IDC_NORMAL_COLOR; ++id)
        InvalidateRect(GetDlgItem(dialog, id), nullptr, FALSE);
    InvalidateRect(GetDlgItem(dialog, IDC_ADVANCED), nullptr, FALSE);
    InvalidateRect(GetDlgItem(dialog, IDC_MODE_PREVIEW), nullptr, FALSE);
    return true;
}

void StyleSettingsCaption(HWND dialog)
{
    if (!settingsHighContrast)
    {
        const COLORREF border = CardEdge;
        DwmSetWindowAttribute(dialog, DWMWA_CAPTION_COLOR, &CaptionSurface, sizeof(CaptionSurface));
        DwmSetWindowAttribute(dialog, DWMWA_TEXT_COLOR, &TextColor, sizeof(TextColor));
        DwmSetWindowAttribute(dialog, DWMWA_BORDER_COLOR, &border, sizeof(border));
    }
    const HICON icon = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDI_TASKBAR));
    SendMessageW(dialog, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(icon));
}

bool IsHighContrast()
{
    HIGHCONTRASTW contrast{};
    contrast.cbSize = sizeof(contrast);
    return SystemParametersInfoW(SPI_GETHIGHCONTRAST, contrast.cbSize, &contrast, 0)
        && (contrast.dwFlags & HCF_HIGHCONTRASTON);
}

bool IsModeButton(int id, int first)
{
    return id >= first && id <= first + static_cast<int>(TaskbarMode::Color);
}

void InitializeSettingsTheme(HWND dialog)
{
    settingsHighContrast = IsHighContrast();
    const COLORREF surface = settingsHighContrast ? GetSysColor(COLOR_WINDOW) : SettingsSurface;
    settingsBackground = CreateSolidBrush(surface);
    StyleSettingsCaption(dialog);
    LOGFONTW font{};
    const auto currentFont = reinterpret_cast<HFONT>(SendMessageW(dialog, WM_GETFONT, 0, 0));
    if (GetObjectW(currentFont, sizeof(font), &font))
    {
        font.lfQuality = CLEARTYPE_NATURAL_QUALITY;
        LOGFONTW labelFont = font;
        labelFont.lfHeight = MulDiv(font.lfHeight, 13, 10);
        labelFont.lfWeight = FW_SEMIBOLD;
        modeLabelFont = CreateFontIndirectW(&labelFont);
        wcscpy_s(font.lfFaceName, L"Segoe UI Variable Display");
        font.lfHeight = MulDiv(font.lfHeight, 21, 10);
        font.lfWeight = FW_SEMIBOLD;
        settingsTitle = CreateFontIndirectW(&font);
        if (settingsTitle)
            SendDlgItemMessageW(dialog, IDC_TITLE, WM_SETFONT, reinterpret_cast<WPARAM>(settingsTitle), FALSE);
    }
}

void ReleaseSettingsTheme()
{
    selectedPage = -1;
    transitionStarted = 0;
    if (settingsTitle) DeleteObject(settingsTitle);
    if (modeLabelFont) DeleteObject(modeLabelFont);
    if (settingsBackground) DeleteObject(settingsBackground);
    settingsTitle = nullptr;
    modeLabelFont = nullptr;
    settingsBackground = nullptr;
}

INT_PTR PaintSettingsControl(UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == WM_NOTIFY)
    {
        const auto* notification = reinterpret_cast<const NMHDR*>(lParam);
        if (notification && notification->idFrom == IDC_OPACITY && notification->code == NM_CUSTOMDRAW)
        {
            const auto& draw = *reinterpret_cast<const NMCUSTOMDRAW*>(lParam);
            SetWindowLongPtrW(GetParent(notification->hwndFrom), DWLP_MSGRESULT, DrawOpacitySlider(draw));
            return TRUE;
        }
    }
    if (message == WM_CTLCOLORDLG)
        return reinterpret_cast<INT_PTR>(settingsBackground);
    if (message == WM_CTLCOLORSTATIC)
    {
        const auto dc = reinterpret_cast<HDC>(wParam);
        SetBkMode(dc, TRANSPARENT);
        const int id = GetDlgCtrlID(reinterpret_cast<HWND>(lParam));
        const bool heading = id == IDC_TITLE;
        const auto text = heading || id == IDC_OPACITY_LABEL ? TextColor : SubtleText;
        SetTextColor(dc, settingsHighContrast ? GetSysColor(COLOR_WINDOWTEXT) : text);
        return reinterpret_cast<INT_PTR>(GetDlgItem(GetParent(reinterpret_cast<HWND>(lParam)), IDC_TITLE)
            ? GetStockObject(HOLLOW_BRUSH) : settingsBackground);
    }
    if (message == WM_DRAWITEM)
    {
        const auto item = reinterpret_cast<const DRAWITEMSTRUCT*>(lParam);
        if (item && item->CtlID == IDC_OPACITY_VALUE)
        {
            DrawOpacityValue(*item);
            return TRUE;
        }
        if (item && item->CtlID == IDC_MODE_PREVIEW)
        {
            DrawModePreview(*item);
            return TRUE;
        }
        if (item && item->CtlType == ODT_BUTTON)
        {
            DrawSettingsButton(*item);
            return TRUE;
        }
    }
    return FALSE;
}
