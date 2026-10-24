module Xaml.Controls;

import std;
import winrt_base;
import Xaml.Runtime;

namespace xaml::controls
{
    runtime::DependencyProperty<runtime::Thickness> const ContentPresenter::PaddingProperty =
        runtime::DependencyProperty<runtime::Thickness>::Register<ContentPresenter>("Padding", {}, runtime::PropertyEffects::Measure);
    void ContentPresenter::Content(runtime::UIElement* value) { SetSingleVisualChild(value); }
    winrt::com_ptr<runtime::UIElement> ContentPresenter::Content() const
    {
        return VisualChildrenCount() ? VisualChildAt(0) : nullptr;
    }
    runtime::Size ContentPresenter::MeasureOverride(runtime::Size available)
    {
        auto const padding = GetValue(PaddingProperty);
        auto child = Content();
        if (!child) { return { padding.left + padding.right, padding.top + padding.bottom }; }
        child->Measure({ std::max(0.0, available.width - padding.left - padding.right),
            std::max(0.0, available.height - padding.top - padding.bottom) });
        auto desired = child->DesiredSize();
        return { desired.width + padding.left + padding.right, desired.height + padding.top + padding.bottom };
    }
    runtime::Size ContentPresenter::ArrangeOverride(runtime::Size finalSize)
    {
        auto const padding = GetValue(PaddingProperty);
        if (auto child = Content())
        {
            child->Arrange({ padding.left, padding.top,
                std::max(0.0, finalSize.width - padding.left - padding.right),
                std::max(0.0, finalSize.height - padding.top - padding.bottom) });
        }
        return finalSize;
    }
}
