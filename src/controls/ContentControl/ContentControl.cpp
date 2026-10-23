module Xaml.Controls;

import std;
import winrt_base;
import Xaml.Runtime;

namespace xaml::controls
{
    winrt::com_ptr<runtime::UIElement> ContentControl::Content() const
    {
        return VisualChildrenCount() == 0 ? nullptr : VisualChildAt(0);
    }

    void ContentControl::Content(runtime::UIElement* content) { SetSingleVisualChild(content); }

    runtime::Size ContentControl::MeasureOverride(runtime::Size available)
    {
        auto const padding = Padding();
        auto const horizontal = padding.left + padding.right;
        auto const vertical = padding.top + padding.bottom;
        auto const content = Content();
        if (!content) { return { horizontal, vertical }; }
        content->Measure({ std::max(0.0, available.width - horizontal),
            std::max(0.0, available.height - vertical) });
        auto const desired = content->DesiredSize();
        return { desired.width + horizontal, desired.height + vertical };
    }

    runtime::Size ContentControl::ArrangeOverride(runtime::Size finalSize)
    {
        if (auto content = Content())
        {
            auto const padding = Padding();
            content->Arrange({ padding.left, padding.top,
                std::max(0.0, finalSize.width - padding.left - padding.right),
                std::max(0.0, finalSize.height - padding.top - padding.bottom) });
        }
        return finalSize;
    }

}
