#include "ColorSpectrum.h"

#include <algorithm>
#include <cmath>

COLORREF HsvColor::ToRgb() const
{
    const double sector = std::fmod(hue, 360.0) / 60.0;
    const double chroma = value * saturation;
    const double second = chroma * (1.0 - std::abs(std::fmod(sector, 2.0) - 1.0));
    const double offset = value - chroma;
    const auto channel = [offset](double component)
    {
        return static_cast<BYTE>(std::lround((component + offset) * 255.0));
    };

    switch (static_cast<int>(sector))
    {
    case 0: return RGB(channel(chroma), channel(second), channel(0));
    case 1: return RGB(channel(second), channel(chroma), channel(0));
    case 2: return RGB(channel(0), channel(chroma), channel(second));
    case 3: return RGB(channel(0), channel(second), channel(chroma));
    case 4: return RGB(channel(second), channel(0), channel(chroma));
    default: return RGB(channel(chroma), channel(0), channel(second));
    }
}

HsvColor HsvColor::FromRgb(COLORREF color, double previousHue)
{
    const double red = GetRValue(color) / 255.0;
    const double green = GetGValue(color) / 255.0;
    const double blue = GetBValue(color) / 255.0;
    const double high = (std::max)({red, green, blue});
    const double low = (std::min)({red, green, blue});
    const double range = high - low;
    if (range == 0)
        return {previousHue, 0, high};

    double hue = high == red ? (green - blue) / range
        : high == green ? 2 + (blue - red) / range : 4 + (red - green) / range;
    if (hue < 0)
        hue += 6;
    return {hue * 60, range / high, high};
}

namespace
{
DWORD BitmapPixel(COLORREF color)
{
    return (static_cast<DWORD>(GetRValue(color)) << 16)
        | (static_cast<DWORD>(GetGValue(color)) << 8) | GetBValue(color);
}

void DrawBitmap(HDC dc, const RECT& bounds, LONG rows, const DWORD* pixels)
{
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = bounds.right - bounds.left;
    info.bmiHeader.biHeight = -rows;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    StretchDIBits(dc, bounds.left, bounds.top, info.bmiHeader.biWidth, bounds.bottom - bounds.top,
        0, 0, info.bmiHeader.biWidth, rows, pixels, &info, DIB_RGB_COLORS, SRCCOPY);
}
}

void ColorSpectrum::Draw(HDC dc, const RECT& bounds, const HsvColor& color, bool hueOnly, bool focused)
{
    const LONG width = bounds.right - bounds.left;
    const LONG height = bounds.bottom - bounds.top;
    if (width < 2 || height < 2)
        return;

    if (hueOnly)
    {
        if (hueRow_.size() != static_cast<size_t>(width))
        {
            hueRow_.resize(width);
            for (LONG x = 0; x < width; ++x)
                hueRow_[x] = BitmapPixel(HsvColor{360.0 * x / (width - 1), 1, 1}.ToRgb());
        }
        DrawBitmap(dc, bounds, 1, hueRow_.data());
    }
    else
    {
        if (lastHue_ != color.hue || fieldWidth_ != width || fieldHeight_ != height)
        {
            field_.resize(static_cast<size_t>(width) * height);
            const COLORREF hue = HsvColor{color.hue, 1, 1}.ToRgb();
            const double red = 255 - GetRValue(hue);
            const double green = 255 - GetGValue(hue);
            const double blue = 255 - GetBValue(hue);
            auto pixel = field_.begin();
            for (LONG y = 0; y < height; ++y)
            {
                const double value = 1.0 - static_cast<double>(y) / (height - 1);
                for (LONG x = 0; x < width; ++x)
                {
                    const double saturation = static_cast<double>(x) / (width - 1);
                    const auto channel = [value, saturation](double difference)
                    {
                        return static_cast<BYTE>((255.0 - saturation * difference) * value + 0.5);
                    };
                    *pixel++ = BitmapPixel(RGB(channel(red), channel(green), channel(blue)));
                }
            }
            fieldWidth_ = width;
            fieldHeight_ = height;
            lastHue_ = color.hue;
        }
        DrawBitmap(dc, bounds, height, field_.data());
    }

    const int saved = SaveDC(dc);
    if (!saved)
        return;
    const int radius = MulDiv(6, GetDeviceCaps(dc, LOGPIXELSX), 96);
    const LONG x = bounds.left + static_cast<LONG>(std::lround((hueOnly ? color.hue / 360 : color.saturation) * (width - 1)));
    const LONG y = bounds.top + static_cast<LONG>(std::lround((1 - color.value) * (height - 1)));
    SelectObject(dc, GetStockObject(DC_PEN));
    SelectObject(dc, GetStockObject(HOLLOW_BRUSH));
    for (const int inset : {0, 1})
    {
        SetDCPenColor(dc, inset ? RGB(255, 255, 255) : RGB(0, 0, 0));
        if (hueOnly)
            RoundRect(dc, x - radius + inset, bounds.top + inset, x + radius + 1 - inset, bounds.bottom - inset, 4, 4);
        else
            Ellipse(dc, x - radius + inset, y - radius + inset, x + radius + 1 - inset, y + radius + 1 - inset);
    }
    if (focused)
        DrawFocusRect(dc, &bounds);
    RestoreDC(dc, saved);
}
