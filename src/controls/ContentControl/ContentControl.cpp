module Xaml.Controls;

import std;
import winrt_base;
import Xaml.Runtime;

namespace xaml::controls
{
    winrt::com_ptr<runtime::UIElement> ContentControl::Content() const
    {
        VerifyAccess(); return m_content;
    }

    void ContentControl::Content(runtime::UIElement* content)
    {
        VerifyAccess();
        winrt::com_ptr<runtime::UIElement> next;
        next.copy_from(content);
        if (next == m_content) { return; }
        if (next && next->Parent() && next->Parent().get() != this) { throw winrt::hresult_invalid_argument(); }
        m_content = std::move(next);
        if (HasTemplateRoot()) { OnApplyTemplate(); }
        else { SetSingleVisualChild(content); }
        InvalidateMeasure();
    }

    void ContentControl::OnApplyTemplate()
    {
        Control::OnApplyTemplate();
        if (auto part = GetTemplateChild(L"ContentPresenter"))
        {
            if (auto presenter = dynamic_cast<ContentPresenter*>(part.get())) { presenter->Content(m_content.get()); }
        }
    }

    void ContentControl::OnTemplateClearing()
    {
        if (auto part = GetTemplateChild(L"ContentPresenter"))
        {
            if (auto presenter = dynamic_cast<ContentPresenter*>(part.get())) { presenter->Content(nullptr); }
        }
    }

    runtime::Size ContentControl::MeasureOverride(runtime::Size available)
    {
        (void)ApplyTemplate();
        if (HasTemplateRoot()) { return Control::MeasureOverride(available); }
        if (m_content && !m_content->Parent()) { SetSingleVisualChild(m_content.get()); }
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
        if (HasTemplateRoot()) { return Control::ArrangeOverride(finalSize); }
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
