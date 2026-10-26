module Xaml.Controls;

import std;
import winrt_base;
import Xaml.Runtime;

namespace xaml::controls
{
    constexpr auto textEffects = runtime::PropertyEffects::Measure | runtime::PropertyEffects::Render;
    runtime::DependencyProperty<winrt::hstring> const TextBlock::TextProperty =
        runtime::DependencyProperty<winrt::hstring>::Register<TextBlock>("Text", {}, textEffects);
    runtime::DependencyProperty<winrt::hstring> const TextBlock::FontFamilyProperty =
        runtime::DependencyProperty<winrt::hstring>::Register<TextBlock>("FontFamily", L"Segoe UI", textEffects,
            +[](winrt::hstring value) { return !value.empty(); });
    runtime::DependencyProperty<winrt::hstring> const TextBlock::LanguageProperty =
        runtime::DependencyProperty<winrt::hstring>::Register<TextBlock>("Language", L"en-US", textEffects,
            +[](winrt::hstring value) { return !value.empty(); });
    runtime::DependencyProperty<double> const TextBlock::FontSizeProperty =
        runtime::DependencyProperty<double>::Register<TextBlock>("FontSize", 14, textEffects,
            +[](double value) { return std::isfinite(value) && value > 0 && value <= 16384; });
    runtime::DependencyProperty<int> const TextBlock::FontWeightProperty =
        runtime::DependencyProperty<int>::Register<TextBlock>("FontWeight", 400, textEffects,
            +[](int value) { return value >= 1 && value <= 999; });
    runtime::DependencyProperty<bool> const TextBlock::TextWrappingProperty =
        runtime::DependencyProperty<bool>::Register<TextBlock>("TextWrapping", false, textEffects);
    runtime::DependencyProperty<runtime::Color> const TextBlock::ForegroundProperty =
        runtime::DependencyProperty<runtime::Color>::Register<TextBlock>("Foreground", runtime::Color::FromARGB(0xFF181818), runtime::PropertyEffects::Render);

    void TextBlock::Text(winrt::hstring const& value) { SetValue(TextProperty, value); }
    winrt::hstring TextBlock::Text() const { return GetValue(TextProperty); }
    winrt::hstring TextBlock::AutomationLabel() const
    {
        auto name = AutomationName();
        return name.empty() ? Text() : name;
    }
    runtime::TextFormat TextBlock::Format() const
    {
        return { .family = std::wstring(GetValue(FontFamilyProperty)), .locale = std::wstring(GetValue(LanguageProperty)),
            .size = GetValue(FontSizeProperty), .weight = static_cast<std::uint16_t>(GetValue(FontWeightProperty)),
            .wrap = GetValue(TextWrappingProperty), .rightToLeft = IsRightToLeft() };
    }
    runtime::Size TextBlock::MeasureOverride(runtime::Size available)
    {
        auto metrics = runtime::MeasureText(std::wstring(Text()), Format(), available.width, available.height);
        return { metrics.width, metrics.height };
    }
    void TextBlock::OnRender(runtime::DrawingContext& context) const
    {
        auto size = ActualSize();
        context.Text(0, 0, size.width, size.height, std::wstring(Text()), Format(), GetValue(ForegroundProperty));
    }
}
