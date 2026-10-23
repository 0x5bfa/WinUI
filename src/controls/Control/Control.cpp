module Xaml.Controls;

import std;
import winrt_base;
import Xaml.Runtime;

namespace xaml::controls
{
    runtime::DependencyProperty<runtime::Thickness> const Control::PaddingProperty =
        runtime::DependencyProperty<runtime::Thickness>::Register<Control>("Padding", {},
            runtime::PropertyEffects::Measure | runtime::PropertyEffects::Render,
            +[](runtime::Thickness value) noexcept
            {
                return std::isfinite(value.left) && value.left >= 0 &&
                    std::isfinite(value.top) && value.top >= 0 &&
                    std::isfinite(value.right) && value.right >= 0 &&
                    std::isfinite(value.bottom) && value.bottom >= 0 &&
                    std::isfinite(value.left + value.right) &&
                    std::isfinite(value.top + value.bottom);
            });

    runtime::Thickness Control::Padding() const { return GetValue(PaddingProperty); }
    void Control::Padding(runtime::Thickness value) { SetValue(PaddingProperty, value); }

    bool Control::IsFocused() const { VerifyAccess(); return m_focused; }

    void Control::SetFocused(bool focused)
    {
        VerifyAccess();
        auto lifetime = get_strong();
        focused = focused && IsEnabled();
        if (focused == m_focused) { return; }
        m_focused = focused;
        InvalidateRender();
        if (!focused) { OnFocusLost(); }
    }

    void Control::OnIsEnabledChanged()
    {
        if (!IsEnabled()) { SetFocused(false); }
    }

    void Control::OnVisualParentChanged() { SetFocused(false); }
    void Control::OnVisualAncestryChanged() { SetFocused(false); }

}
