module Xaml.Controls;

import std;
import winrt_base;
import Xaml.Runtime;

namespace xaml::controls
{
    runtime::DependencyProperty<runtime::Color> const Control::BackgroundProperty =
        runtime::DependencyProperty<runtime::Color>::Register<Control>("Background", runtime::Color::FromARGB(0xFFF3F3F3), runtime::PropertyEffects::Render);
    runtime::DependencyProperty<runtime::Color> const Control::ForegroundProperty =
        runtime::DependencyProperty<runtime::Color>::Register<Control>("Foreground", runtime::Color::FromARGB(0xFF181818), runtime::PropertyEffects::Render);
    runtime::DependencyProperty<runtime::Color> const Control::BorderBrushProperty =
        runtime::DependencyProperty<runtime::Color>::Register<Control>("BorderBrush", runtime::Color::FromARGB(0xFF898989), runtime::PropertyEffects::Render);
    runtime::DependencyProperty<double> const Control::BorderThicknessProperty =
        runtime::DependencyProperty<double>::Register<Control>("BorderThickness", 1, runtime::PropertyEffects::Measure,
            +[](double value) { return std::isfinite(value) && value >= 0; });
    runtime::DependencyProperty<double> const Control::CornerRadiusProperty =
        runtime::DependencyProperty<double>::Register<Control>("CornerRadius", 4, runtime::PropertyEffects::Render,
            +[](double value) { return std::isfinite(value) && value >= 0; });
    runtime::DependencyProperty<bool> const Control::IsTabStopProperty =
        runtime::DependencyProperty<bool>::Register<Control>("IsTabStop", true, runtime::PropertyEffects::Render);
    runtime::DependencyProperty<int> const Control::TabIndexProperty =
        runtime::DependencyProperty<int>::Register<Control>("TabIndex", 0, runtime::PropertyEffects::None,
            +[](int value) { return value >= 0; });
    bool Control::IsTabStop() const { return GetValue(IsTabStopProperty); }
    void Control::IsTabStop(bool value) { SetValue(IsTabStopProperty, value); }
    int Control::TabIndex() const { return GetValue(TabIndexProperty); }
    void Control::TabIndex(int value) { SetValue(TabIndexProperty, value); }
    bool Control::IsKeyboardFocusable() const { return IsTabStop(); }
    int Control::KeyboardTabIndex() const { return TabIndex(); }
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

    bool Control::IsFocused() const { VerifyAccess(); return InputContext() ? HasFocus() : m_focused; }

    void Control::SetFocused(bool focused)
    {
        VerifyAccess();
        auto lifetime = get_strong();
        if (auto input = InputContext())
        {
            if (focused) { (void)input->Focus(this); return; }
            if (input->FocusedElement().get() == this) { (void)input->Focus(nullptr); return; }
        }
        focused = focused && IsEnabled();
        if (focused == m_focused) { return; }
        m_focused = focused;
        InvalidateRender();
        if (!focused) { OnFocusLost(); }
        UpdateVisualState();
    }

    void Control::OnInputEvent(runtime::InputEventArgs& args)
    {
        if (args.originalSource == this && (args.event == runtime::InputEvent::GotFocus || args.event == runtime::InputEvent::LostFocus))
        {
            m_focused = args.event == runtime::InputEvent::GotFocus;
            if (!m_focused) { OnFocusLost(); }
            InvalidateRender();
            UpdateVisualState();
        }
    }

    void Control::OnPropertyChanged(runtime::PropertyChange const& change)
    {
        FrameworkElement::OnPropertyChanged(change);
        if (change.property == TemplateProperty.Id()) { ++m_templateVersion; ClearTemplate(); }
        if (change.property == IsTabStopProperty.Id() && !IsTabStop()) { SetFocused(false); }
        UpdateVisualState();
    }

    void Control::OnIsEnabledChanged()
    {
        if (!IsEnabled()) { SetFocused(false); }
        UpdateVisualState();
    }

    void Control::OnVisualParentChanged() { FrameworkElement::OnVisualParentChanged(); SetFocused(false); }
    void Control::OnVisualAncestryChanged() { FrameworkElement::OnVisualAncestryChanged(); SetFocused(false); }

}
