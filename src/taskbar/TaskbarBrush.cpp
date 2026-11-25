#include "TaskbarBrush.h"
#include "TaskbarSession.h"
#include <winrt/Windows.UI.Xaml.Media.h>

namespace xaml = winrt::Windows::UI::Xaml;

TaskbarBrush::TaskbarBrush(const xaml::Shapes::Rectangle& background)
    : background_(background),
      previousFill_(background.ReadLocalValue(xaml::Shapes::Shape::FillProperty()))
{}

void TaskbarBrush::Apply(LONG64 value, bool border)
{
    const auto mode = AppearanceMode(value);
    if (mode == TaskbarMode::Default)
    {
        Restore();
        return;
    }
    if (!border && mode == TaskbarMode::Blur)
    {
        xaml::Media::AcrylicBrush brush;
        brush.BackgroundSource(xaml::Media::AcrylicBackgroundSource::Backdrop);
        brush.TintColor({255, 24, 29, 38});
        brush.FallbackColor({255, 34, 40, 50});
        brush.TintOpacity(0.20);
        background_.Fill(brush);
        return;
    }
    const COLORREF color = static_cast<COLORREF>(value);
    const auto alpha = static_cast<BYTE>(!border && mode == TaskbarMode::Color ? AppearanceAlpha(value) : 0);
    xaml::Media::SolidColorBrush brush;
    brush.Color({alpha, GetRValue(color), GetGValue(color), GetBValue(color)});
    background_.Fill(brush);
}

void TaskbarBrush::Restore()
{
    const auto property = xaml::Shapes::Shape::FillProperty();
    if (previousFill_ == xaml::DependencyProperty::UnsetValue())
        background_.ClearValue(property);
    else
        background_.SetValue(property, previousFill_);
}

bool IsInsideTaskbarFrame(const xaml::Shapes::Rectangle& rectangle)
{
    if (!rectangle)
        return false;

    for (auto parent = xaml::Media::VisualTreeHelper::GetParent(rectangle);
         parent;
         parent = xaml::Media::VisualTreeHelper::GetParent(parent))
    {
        if (const auto element = parent.try_as<xaml::FrameworkElement>();
            element && element.Name() == L"HoverFlyoutBackgroundControl")
            return false;

        if (winrt::get_class_name(parent) == L"Taskbar.TaskbarFrame")
            return true;
    }

    return false;
}
