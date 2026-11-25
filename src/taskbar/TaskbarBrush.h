#pragma once

#include <windows.h>

#undef GetCurrentTime
#include <winrt/Windows.UI.Xaml.Shapes.h>

class TaskbarBrush
{
public:
    explicit TaskbarBrush(const winrt::Windows::UI::Xaml::Shapes::Rectangle& background);
    void Apply(LONG64 value, bool border);
    void Restore();

private:
    winrt::Windows::UI::Xaml::Shapes::Rectangle background_;
    winrt::Windows::Foundation::IInspectable previousFill_;
};

bool IsInsideTaskbarFrame(const winrt::Windows::UI::Xaml::Shapes::Rectangle& rectangle);
