module Xaml.Controls;

import std;
import winrt_base;
import Xaml.Runtime;

namespace xaml::controls
{
    runtime::DependencyProperty<std::shared_ptr<runtime::Command>> const ButtonBase::CommandProperty =
        runtime::DependencyProperty<std::shared_ptr<runtime::Command>>::Register<ButtonBase>("Command", {});
    runtime::DependencyProperty<winrt::Windows::Foundation::IInspectable> const ButtonBase::CommandParameterProperty =
        runtime::DependencyProperty<winrt::Windows::Foundation::IInspectable>::Register<ButtonBase>("CommandParameter", nullptr);
    ButtonBase::~ButtonBase()
    {
        try { if (auto command = m_observedCommand.lock()) { command->Unsubscribe(m_commandToken); } } catch (...) {}
    }
    bool ButtonBase::IsEnabledCore() const { return m_commandCanExecute; }
    void ButtonBase::RefreshCommand()
    {
        auto lifetime = get_strong(); auto version = ++m_commandVersion;
        auto command = GetValue(CommandProperty);
        if (m_observedCommand.lock() != command)
        {
            if (auto previous = m_observedCommand.lock()) { previous->Unsubscribe(m_commandToken); }
            m_commandToken = 0; m_observedCommand = command;
            if (command)
            {
                auto weak = runtime::DependencyObject::get_weak();
                m_commandToken = command->Subscribe([weak]
                { if (auto owner = weak.get()) { dynamic_cast<ButtonBase&>(*owner).RefreshCommand(); } });
            }
        }
        bool const enabled = !command || command->CanExecute(GetValue(CommandParameterProperty));
        if (version != m_commandVersion || m_commandCanExecute == enabled) { return; }
        m_commandCanExecute = enabled;
        InvalidateIsEnabled();
    }
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
        if (auto command = GetValue(CommandProperty)) { command->Execute(GetValue(CommandParameterProperty)); }
    }

    void ButtonBase::CancelInteraction()
    {
        VerifyAccess();
        auto pointer = std::exchange(m_pointer, {});
        m_key.reset();
        m_pointerOver = false;
        if (pointer) { ReleasePointerCapture(*pointer); }
        InvalidateRender();
        UpdateVisualState();
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
        Control::OnPropertyChanged(change);
        if (change.property == CommandProperty.Id() || change.property == CommandParameterProperty.Id()) { RefreshCommand(); }
        if (change.property == ClickModeProperty.Id()) { CancelInteraction(); }
    }

    void ButtonBase::UpdateVisualState()
    {
        Control::UpdateVisualState();
        (void)GoToState(!IsEnabled() ? L"Disabled" : IsPressed() ? L"Pressed" : IsPointerOver() ? L"PointerOver" : L"Normal");
    }

    void ButtonBase::OnInputEvent(runtime::InputEventArgs& args)
    {
        Control::OnInputEvent(args);
        auto lifetime = get_strong();
        if (args.event == runtime::InputEvent::KeyDown || args.event == runtime::InputEvent::KeyUp)
        {
            if ((args.modifiers & 6) != 0) { return; }
            if (args.virtualKey == 32 || args.virtualKey == 13 || args.virtualKey == 195)
            {
                args.handled = ProcessKey(args.virtualKey == 13 ? ActivationKey::Enter : ActivationKey::Space,
                    args.event == runtime::InputEvent::KeyDown) || args.handled;
                UpdateVisualState();
            }
            return;
        }
        std::optional<PointerAction> action;
        switch (args.event)
        {
        case runtime::InputEvent::PointerEntered: action = PointerAction::Entered; break;
        case runtime::InputEvent::PointerExited: action = PointerAction::Exited; break;
        case runtime::InputEvent::PointerPressed: action = PointerAction::Pressed; break;
        case runtime::InputEvent::PointerMoved: action = PointerAction::Moved; break;
        case runtime::InputEvent::PointerReleased: action = PointerAction::Released; break;
        case runtime::InputEvent::PointerCanceled: action = PointerAction::Cancelled; break;
        case runtime::InputEvent::PointerCaptureLost: action = PointerAction::CaptureLost; break;
        default: return;
        }
        auto point = PointFromRoot(args.position);
        auto size = ActualSize();
        bool const over = point.x >= 0 && point.y >= 0 && point.x < size.width && point.y < size.height &&
            args.event != runtime::InputEvent::PointerExited;
        if (*action == PointerAction::Pressed && args.primaryButton && over && IsEnabled() && ClickMode() != controls::ClickMode::Hover)
        {
            if (m_pointer || m_key) { return; }
            (void)Focus();
            if (!IsEnabled() || !InputContext()) { return; }
            if (!CapturePointer(args.pointerId)) { return; }
        }
        args.handled = ProcessPointer({ *action, args.pointerId, over, args.primaryButton }) || args.handled;
        UpdateVisualState();
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
            auto pointer = std::exchange(m_pointer, {});
            m_pointerOver = input.isOver;
            if (pointer) { ReleasePointerCapture(*pointer); }
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
