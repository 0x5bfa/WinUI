module Xaml.Controls;

import std;
import winrt_base;
import Xaml.Runtime;

namespace xaml::controls
{
    runtime::DependencyProperty<controls::ClickMode> const ButtonBase::ClickModeProperty =
        runtime::DependencyProperty<controls::ClickMode>::Register<ButtonBase>("ClickMode", controls::ClickMode::Release,
            runtime::PropertyEffects::Render, +[](controls::ClickMode value)
            {
                return value == controls::ClickMode::Release || value == controls::ClickMode::Press ||
                    value == controls::ClickMode::Hover;
            });

    controls::ClickMode ButtonBase::ClickMode() const { return GetValue(ClickModeProperty); }
    void ButtonBase::ClickMode(controls::ClickMode value) { SetValue(ClickModeProperty, value); }

    bool ButtonBase::IsPressed() const
    {
        VerifyAccess();
        return IsEnabled() && (ClickMode() == controls::ClickMode::Hover ? m_pointerOver :
            (m_key.has_value() || (m_pointer.has_value() && m_pointerOver)));
    }

    bool ButtonBase::IsPointerOver() const { VerifyAccess(); return m_pointerOver; }
    std::optional<std::uint32_t> ButtonBase::ActivePointer() const { VerifyAccess(); return m_pointer; }

    ButtonBase::EventToken ButtonBase::AddClick(ClickHandler handler)
    {
        VerifyAccess();
        auto lifetime = get_strong();
        if (!handler) { throw winrt::hresult_invalid_argument(); }
        if (m_nextToken == std::numeric_limits<EventToken>::max()) { throw winrt::hresult_out_of_bounds(); }
        auto const token = m_nextToken + 1;
        m_click.push_back(std::make_shared<Subscription>(token, std::move(handler)));
        m_nextToken = token;
        return token;
    }

    void ButtonBase::RemoveClick(EventToken token)
    {
        VerifyAccess();
        auto lifetime = get_strong();
        auto const found = std::ranges::find_if(m_click, [token](auto const& entry) { return entry->token == token; });
        if (found != m_click.end())
        {
            // Retire the callback after erasing its registration: destroying a
            // capture may itself run user code and reenter registration.
            auto removed = std::move(*found);
            removed->active = false;
            m_click.erase(found);
        }
    }

    void ButtonBase::OnClick()
    {
        auto lifetime = get_strong();
        auto const snapshot = m_click;
        for (auto const& entry : snapshot)
        {
            if (entry->active) { entry->handler(*this); }
        }
    }

    void ButtonBase::CancelInteraction()
    {
        VerifyAccess();
        m_pointer.reset();
        m_key.reset();
        m_pointerOver = false;
        InvalidateRender();
    }

    void ButtonBase::OnFocusLost() { CancelInteraction(); }

    void ButtonBase::OnIsEnabledChanged()
    {
        Control::OnIsEnabledChanged();
        if (!IsEnabled()) { CancelInteraction(); }
    }

    void ButtonBase::OnVisualParentChanged()
    {
        Control::OnVisualParentChanged();
        CancelInteraction();
    }

    void ButtonBase::OnVisualAncestryChanged()
    {
        Control::OnVisualAncestryChanged();
        CancelInteraction();
    }

    void ButtonBase::OnPropertyChanged(runtime::PropertyChange const& change)
    {
        runtime::UIElement::OnPropertyChanged(change);
        if (change.property == ClickModeProperty.Id()) { CancelInteraction(); }
    }

    bool ButtonBase::ProcessPointer(PointerInput const& input)
    {
        VerifyAccess();
        auto lifetime = get_strong();
        if (!IsEnabled() || (m_pointer && *m_pointer != input.id)) { return false; }
        auto const mode = ClickMode();
        switch (input.action)
        {
        case PointerAction::Entered:
        {
            auto const wasOver = m_pointerOver;
            m_pointerOver = input.isOver;
            InvalidateRender();
            if (mode == controls::ClickMode::Hover && m_pointerOver && !wasOver) { OnClick(); return true; }
            return false;
        }
        case PointerAction::Exited:
            m_pointerOver = false;
            InvalidateRender();
            return m_pointer.has_value();
        case PointerAction::Moved:
            m_pointerOver = input.isOver;
            InvalidateRender();
            return m_pointer.has_value();
        case PointerAction::Pressed:
            if (!input.isPrimaryButton || !input.isOver || mode == controls::ClickMode::Hover || m_pointer || m_key)
            {
                return false;
            }
            m_pointer = input.id;
            m_pointerOver = true;
            SetFocused(true);
            InvalidateRender();
            if (mode == controls::ClickMode::Press) { OnClick(); }
            return true;
        case PointerAction::Released:
        {
            if (!m_pointer || !input.isPrimaryButton) { return false; }
            bool const click = IsPressed() && input.isOver && mode == controls::ClickMode::Release;
            m_pointer.reset();
            m_pointerOver = input.isOver;
            InvalidateRender();
            // Complete the input transition before invoking reentrant app code.
            if (click) { OnClick(); }
            return true;
        }
        case PointerAction::Cancelled:
        case PointerAction::CaptureLost:
            if (!m_pointer) { return false; }
            CancelInteraction();
            return true;
        }
        return false;
    }

    bool ButtonBase::ProcessKey(ActivationKey key, bool isDown)
    {
        VerifyAccess();
        auto lifetime = get_strong();
        if (key != ActivationKey::Space && key != ActivationKey::Enter) { throw winrt::hresult_invalid_argument(); }
        if (!IsEnabled() || !IsFocused() || ClickMode() == controls::ClickMode::Hover || m_pointer) { return false; }
        if (isDown)
        {
            if (m_key) { return *m_key == key; }
            m_key = key;
            InvalidateRender();
            if (ClickMode() == controls::ClickMode::Press) { OnClick(); }
            return true;
        }
        if (m_key != key) { return false; }
        m_key.reset();
        InvalidateRender();
        if (ClickMode() == controls::ClickMode::Release) { OnClick(); }
        return true;
    }

}
