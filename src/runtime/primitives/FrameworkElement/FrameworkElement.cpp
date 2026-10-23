module Xaml.Runtime;

import std;
import winrt_base;

namespace xaml::runtime
{
    namespace
    {
        bool ValidLength(double value) noexcept
        {
            return std::isnan(value) || (std::isfinite(value) && value >= 0);
        }

        void ValidateSize(Size size, bool allowInfinity)
        {
            auto valid = [allowInfinity](double value)
            {
                return value >= 0 && (allowInfinity || std::isfinite(value));
            };
            if (!valid(size.width) || !valid(size.height))
            {
                throw winrt::hresult_invalid_argument();
            }
        }

    }

    DependencyProperty<double> const FrameworkElement::WidthProperty =
        DependencyProperty<double>::Register<FrameworkElement>("Width", std::numeric_limits<double>::quiet_NaN(),
            PropertyEffects::Measure | PropertyEffects::Render, ValidLength);

    DependencyProperty<double> const FrameworkElement::HeightProperty =
        DependencyProperty<double>::Register<FrameworkElement>("Height", std::numeric_limits<double>::quiet_NaN(),
            PropertyEffects::Measure | PropertyEffects::Render, ValidLength);

    double FrameworkElement::Width() const { return GetValue(WidthProperty); }
    void FrameworkElement::Width(double value) { SetValue(WidthProperty, value); }
    double FrameworkElement::Height() const { return GetValue(HeightProperty); }
    void FrameworkElement::Height(double value) { SetValue(HeightProperty, value); }

    Size FrameworkElement::MeasureCore(Size available)
    {
        auto const width = Width();
        auto const height = Height();
        Size constraint{ std::isnan(width) ? available.width : std::min(width, available.width),
            std::isnan(height) ? available.height : std::min(height, available.height) };
        auto const content = MeasureOverride(constraint);
        ValidateSize(content, false);
        return { std::min(available.width, std::isnan(width) ? content.width : width),
            std::min(available.height, std::isnan(height) ? content.height : height) };
    }

    Size FrameworkElement::ArrangeCore(Size finalSize)
    {
        auto const width = Width();
        auto const height = Height();
        return ArrangeOverride({ std::isnan(width) ? finalSize.width : std::min(width, finalSize.width),
            std::isnan(height) ? finalSize.height : std::min(height, finalSize.height) });
    }

    Size FrameworkElement::MeasureOverride(Size) { return {}; }
    Size FrameworkElement::ArrangeOverride(Size finalSize) { return finalSize; }
}
