module Xaml.Controls;

import std;
import winrt_base;
import Xaml.Runtime;

namespace xaml::controls
{
    runtime::DependencyProperty<runtime::Color> const Border::BackgroundProperty =
        runtime::DependencyProperty<runtime::Color>::Register<Border>("Background", {}, runtime::PropertyEffects::Render);
    runtime::DependencyProperty<runtime::Color> const Border::BorderBrushProperty =
        runtime::DependencyProperty<runtime::Color>::Register<Border>("BorderBrush", {}, runtime::PropertyEffects::Render);
    runtime::DependencyProperty<double> const Border::BorderThicknessProperty =
        runtime::DependencyProperty<double>::Register<Border>("BorderThickness", 0, runtime::PropertyEffects::Measure | runtime::PropertyEffects::Render,
            +[](double value) { return std::isfinite(value) && value >= 0; });
    runtime::DependencyProperty<double> const Border::CornerRadiusProperty =
        runtime::DependencyProperty<double>::Register<Border>("CornerRadius", 0, runtime::PropertyEffects::Render,
            +[](double value) { return std::isfinite(value) && value >= 0; });
    runtime::DependencyProperty<runtime::Thickness> const Border::PaddingProperty =
        runtime::DependencyProperty<runtime::Thickness>::Register<Border>("Padding", {}, runtime::PropertyEffects::Measure,
            +[](runtime::Thickness value)
            {
                return std::isfinite(value.left + value.right) && std::isfinite(value.top + value.bottom) &&
                    value.left >= 0 && value.right >= 0 && value.top >= 0 && value.bottom >= 0;
            });

    void Border::Child(runtime::UIElement* value) { SetSingleVisualChild(value); }
    winrt::com_ptr<runtime::UIElement> Border::Child() const { return VisualChildrenCount() ? VisualChildAt(0) : nullptr; }
    runtime::Size Border::MeasureOverride(runtime::Size available)
    {
        auto padding = GetValue(PaddingProperty);
        double const border = GetValue(BorderThicknessProperty);
        double const horizontal = padding.left + padding.right + 2 * border;
        double const vertical = padding.top + padding.bottom + 2 * border;
        if (auto child = Child())
        {
            child->Measure({ std::max(0.0, available.width - horizontal), std::max(0.0, available.height - vertical) });
            auto desired = child->DesiredSize();
            return { desired.width + horizontal, desired.height + vertical };
        }
        return { horizontal, vertical };
    }
    runtime::Size Border::ArrangeOverride(runtime::Size finalSize)
    {
        if (auto child = Child())
        {
            auto padding = GetValue(PaddingProperty);
            double const border = GetValue(BorderThicknessProperty);
            child->Arrange({ padding.left + border, padding.top + border,
                std::max(0.0, finalSize.width - padding.left - padding.right - 2 * border),
                std::max(0.0, finalSize.height - padding.top - padding.bottom - 2 * border) });
        }
        return finalSize;
    }
    void Border::OnRender(runtime::DrawingContext& context) const
    {
        auto size = ActualSize();
        context.Rectangle(0, 0, size.width, size.height, GetValue(BackgroundProperty), GetValue(CornerRadiusProperty),
            GetValue(BorderBrushProperty), GetValue(BorderThicknessProperty));
    }
}
