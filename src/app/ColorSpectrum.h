#pragma once

#include <windows.h>
#include <vector>

struct HsvColor
{
    double hue{};
    double saturation{};
    double value{};

    COLORREF ToRgb() const;
    static HsvColor FromRgb(COLORREF color, double previousHue = 0);
};

class ColorSpectrum
{
public:
    void Draw(HDC dc, const RECT& bounds, const HsvColor& color, bool hueOnly, bool focused);

private:
    std::vector<DWORD> field_;
    std::vector<DWORD> hueRow_;
    LONG fieldWidth_{};
    LONG fieldHeight_{};
    double lastHue_{-1};
};
