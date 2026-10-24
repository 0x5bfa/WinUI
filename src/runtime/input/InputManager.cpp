module Xaml.Runtime;

import std;
import winrt_base;

namespace xaml::runtime
{
    InputManager::InputManager(UIElement& root)
        : m_thread(std::this_thread::get_id()), m_root(static_cast<DependencyObject&>(root).get_weak())
    {
        root.VerifyAccess();
    }

    void InputManager::VerifyAccess() const
    {
        if (m_thread != std::this_thread::get_id()) { throw winrt::hresult_wrong_thread(); }
    }

    winrt::com_ptr<UIElement> InputManager::Resolve(WeakElement const& weak)
    {
        auto object = weak.get();
        if (!object) { return nullptr; }
        winrt::com_ptr<UIElement> element;
        element.copy_from(dynamic_cast<UIElement*>(object.get()));
        return element;
    }

    bool InputManager::Contains(UIElement const& element) const
    {
        auto root = Resolve(m_root);
        if (!root || !element.IsConnectedToRoot()) { return false; }
        auto current = const_cast<UIElement&>(element).get_strong();
        while (current)
        {
            if (current == root) { return true; }
            current = current->Parent();
        }
        return false;
    }

    bool InputManager::CanReceiveInput(UIElement const& element) const
    {
        if (!Contains(element) || !element.IsEnabled()) { return false; }
        auto current = const_cast<UIElement&>(element).get_strong();
        while (current)
        {
            if (current->VisibilityState() != Visibility::Visible) { return false; }
            current = current->Parent();
        }
        return true;
    }

    InputManager::Route InputManager::SnapshotRoute(UIElement& target) const
    {
        Route route;
        auto root = Resolve(m_root);
        auto current = target.get_strong();
        while (current)
        {
            route.push_back(current);
            if (current == root) { return route; }
            current = current->Parent();
        }
        // A detached target can still receive its own cleanup notification.
        route.resize(1);
        return route;
    }

    bool InputManager::Raise(UIElement& target, InputEventArgs& args, bool bubble)
    {
        auto route = SnapshotRoute(target);
        auto const cancellationVersion = m_cancellationVersion;
        args.originalSource = &target;
        for (auto const& element : route)
        {
            if (element.get() != &target && !Contains(*element)) { continue; }
            element->RaiseInputEvent(args);
            if (!bubble || !m_active || cancellationVersion != m_cancellationVersion) { break; }
        }
        return args.handled;
    }

    bool InputManager::Focus(UIElement* element)
    {
        VerifyAccess();
        if (!m_active || m_resetting || (element && !CanReceiveInput(*element))) { return false; }
        auto previous = Resolve(m_focus);
        winrt::com_ptr<UIElement> next;
        next.copy_from(element);
        if (previous == next) { return true; }
        m_focus = next ? static_cast<DependencyObject&>(*next).get_weak() : WeakElement{};
        if (previous)
        {
            previous->InvalidateRender();
            InputEventArgs args{ .event = InputEvent::LostFocus };
            Raise(*previous, args);
        }
        if (m_active && next && Resolve(m_focus) == next && CanReceiveInput(*next))
        {
            next->InvalidateRender();
            InputEventArgs args{ .event = InputEvent::GotFocus };
            Raise(*next, args);
        }
        ValidateState();
        return !element || Resolve(m_focus) == next;
    }

    winrt::com_ptr<UIElement> InputManager::FocusedElement() const
    {
        VerifyAccess();
        auto element = Resolve(m_focus);
        return element && CanReceiveInput(*element) ? element : nullptr;
    }

    bool InputManager::CapturePointer(UIElement& element, std::uint32_t pointerId)
    {
        VerifyAccess();
        if (!m_active || m_resetting || !m_pressed.contains(pointerId) ||
            !CanReceiveInput(element) || !element.IsHitTestVisible()) { return false; }
        auto existing = m_capture.find(pointerId);
        auto previous = existing == m_capture.end() ? nullptr : Resolve(existing->second);
        if (previous.get() == &element) { return true; }
        m_capture[pointerId] = static_cast<DependencyObject&>(element).get_weak();
        if (previous)
        {
            InputEventArgs args{ .event = InputEvent::PointerCaptureLost, .pointerId = pointerId };
            Raise(*previous, args);
        }
        return CapturedElement(pointerId).get() == &element;
    }

    winrt::com_ptr<UIElement> InputManager::CapturedElement(std::uint32_t pointerId) const
    {
        VerifyAccess();
        auto found = m_capture.find(pointerId);
        if (found == m_capture.end()) { return nullptr; }
        auto element = Resolve(found->second);
        return element && CanReceiveInput(*element) && element->IsHitTestVisible() ? element : nullptr;
    }

    void InputManager::ReleasePointer(std::uint32_t pointerId)
    {
        VerifyAccess();
        auto found = m_capture.find(pointerId);
        if (found == m_capture.end()) { return; }
        auto previous = Resolve(found->second);
        m_capture.erase(found);
        if (previous)
        {
            InputEventArgs args{ .event = InputEvent::PointerCaptureLost, .pointerId = pointerId };
            Raise(*previous, args);
        }
    }

    void InputManager::ValidateState()
    {
        VerifyAccess();
        if (!m_active || m_validating || m_resetting) { return; }
        m_validating = true;
        try
        {
            if (auto focused = Resolve(m_focus); focused && !CanReceiveInput(*focused)) { Focus(nullptr); }
            std::vector<std::uint32_t> invalid;
            for (auto const& [id, target] : m_capture)
            {
                auto element = Resolve(target);
                if (!element || !CanReceiveInput(*element) || !element->IsHitTestVisible()) { invalid.push_back(id); }
            }
            for (auto id : invalid)
            {
                if (!CapturedElement(id)) { ReleasePointer(id); }
            }
            m_validating = false;
        }
        catch (...) { m_validating = false; throw; }
    }

    void InputManager::UpdatePointerOver(winrt::com_ptr<UIElement> target, InputEventArgs const& input)
    {
        auto const cancellationVersion = m_cancellationVersion;
        auto found = m_over.find(input.pointerId);
        auto previous = found == m_over.end() ? nullptr : Resolve(found->second);
        if (previous == target) { return; }
        auto oldRoute = previous ? SnapshotRoute(*previous) : Route{};
        auto newRoute = target ? SnapshotRoute(*target) : Route{};
        if (target) { m_over[input.pointerId] = static_cast<DependencyObject&>(*target).get_weak(); }
        else { m_over.erase(input.pointerId); }
        while (!oldRoute.empty() && !newRoute.empty() && oldRoute.back() == newRoute.back())
        {
            oldRoute.pop_back();
            newRoute.pop_back();
        }
        for (auto const& element : oldRoute)
        {
            auto args = input;
            args.event = InputEvent::PointerExited;
            args.handled = false;
            Raise(*element, args, false);
            if (!m_active || m_resetting || cancellationVersion != m_cancellationVersion) { return; }
        }
        for (auto it = newRoute.rbegin(); it != newRoute.rend(); ++it)
        {
            if (!CanReceiveInput(**it)) { continue; }
            auto args = input;
            args.event = InputEvent::PointerEntered;
            args.handled = false;
            Raise(**it, args, false);
            if (!m_active || m_resetting || cancellationVersion != m_cancellationVersion) { return; }
        }
    }

    bool InputManager::Process(InputEventArgs args)
    {
        VerifyAccess();
        if (!m_active || m_resetting) { return false; }
        ValidateState();
        if (!m_active) { return false; }
        if (args.event == InputEvent::HostFocusLost) { CancelInput(); return false; }
        if (args.event == InputEvent::KeyDown || args.event == InputEvent::KeyUp)
        {
            auto focused = FocusedElement();
            return focused && Raise(*focused, args);
        }
        switch (args.event)
        {
        case InputEvent::PointerEntered: case InputEvent::PointerExited:
        case InputEvent::PointerPressed: case InputEvent::PointerMoved: case InputEvent::PointerReleased:
        case InputEvent::PointerCanceled: case InputEvent::PointerCaptureLost:
        case InputEvent::PointerWheelChanged:
            break;
        default:
            throw winrt::hresult_invalid_argument();
        }
        auto root = Resolve(m_root);
        if (!root || !std::isfinite(args.position.x) || !std::isfinite(args.position.y)) { return false; }
        bool const terminal = (args.event == InputEvent::PointerReleased && !args.inContact) ||
            args.event == InputEvent::PointerCanceled || args.event == InputEvent::PointerCaptureLost;
        if (args.event == InputEvent::PointerPressed) { m_pressed.insert(args.pointerId); }
        auto captured = CapturedElement(args.pointerId);
        auto hit = root->HitTest(args.position);
        if (args.event == InputEvent::PointerExited || args.event == InputEvent::PointerCanceled ||
            args.event == InputEvent::PointerCaptureLost) { hit = nullptr; }
        auto const cancellationVersion = m_cancellationVersion;
        UpdatePointerOver(hit, args);
        if (!m_active || cancellationVersion != m_cancellationVersion) { return false; }
        if (args.event == InputEvent::PointerEntered || args.event == InputEvent::PointerExited) { return false; }
        captured = CapturedElement(args.pointerId);
        if (hit && !CanReceiveInput(*hit)) { hit = nullptr; }
        auto target = captured ? captured : hit;
        if (terminal) { m_pressed.erase(args.pointerId); }
        bool handled{};
        try
        {
            if (args.event == InputEvent::PointerCaptureLost) { ReleasePointer(args.pointerId); }
            else if (target && CanReceiveInput(*target)) { handled = Raise(*target, args); }
        }
        catch (...)
        {
            if (terminal) { m_capture.erase(args.pointerId); }
            throw;
        }
        if (terminal) { ReleasePointer(args.pointerId); }
        ValidateState();
        return handled;
    }

    void InputManager::CancelInput()
    {
        VerifyAccess();
        if (m_resetting) { return; }
        m_resetting = true;
        ++m_cancellationVersion;
        auto captures = std::exchange(m_capture, {});
        auto focus = Resolve(m_focus);
        m_focus = {};
        auto over = std::exchange(m_over, {});
        m_pressed.clear();
        std::exception_ptr failure;
        for (auto const& [id, weak] : captures)
        {
            if (auto element = Resolve(weak))
            {
                try
                {
                    InputEventArgs args{ .event = InputEvent::PointerCaptureLost, .pointerId = id };
                    Raise(*element, args);
                }
                catch (...) { if (!failure) { failure = std::current_exception(); } }
            }
        }
        for (auto const& [id, weak] : over)
        {
            if (auto element = Resolve(weak))
            {
                for (auto const& ancestor : SnapshotRoute(*element))
                {
                    try
                    {
                        InputEventArgs args{ .event = InputEvent::PointerExited, .pointerId = id };
                        Raise(*ancestor, args, false);
                    }
                    catch (...) { if (!failure) { failure = std::current_exception(); } }
                }
            }
        }
        if (focus)
        {
            try
            {
                InputEventArgs args{ .event = InputEvent::LostFocus };
                Raise(*focus, args);
            }
            catch (...) { if (!failure) { failure = std::current_exception(); } }
        }
        m_resetting = false;
        if (failure) { std::rethrow_exception(failure); }
    }

    void InputManager::Shutdown()
    {
        VerifyAccess();
        if (!m_active) { return; }
        m_active = false;
        CancelInput();
    }
}
