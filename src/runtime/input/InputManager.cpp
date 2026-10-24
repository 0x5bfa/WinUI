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
        winrt::com_ptr<UIElement> next;
        next.copy_from(element);
        auto request = m_focusRequest;
        if (next && request && !request()) { return false; }
        if (!m_active || (next && !CanReceiveInput(*next))) { return false; }
        auto previous = Resolve(m_focus);
        if (previous == next) { return true; }
        m_focus = next ? static_cast<DependencyObject&>(*next).get_weak() : WeakElement{};
        for (auto ancestor = next; ancestor; ancestor = ancestor->Parent())
        {
            if (ancestor->GetValue(UIElement::TabFocusNavigationProperty) == KeyboardNavigationMode::Once)
            { ancestor->m_lastTabFocus = m_focus; }
        }
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
        auto observer = m_focusObserver;
        if (observer) { observer(FocusedElement().get()); }
        return !element || Resolve(m_focus) == next;
    }

    winrt::com_ptr<UIElement> InputManager::FocusedElement() const
    {
        VerifyAccess();
        auto element = Resolve(m_focus);
        return element && CanReceiveInput(*element) ? element : nullptr;
    }

    void InputManager::SetFocusRequest(std::function<bool()> request)
    {
        VerifyAccess(); m_focusRequest = std::move(request);
    }
    void InputManager::SetFocusObserver(std::function<void(UIElement*)> observer)
    {
        VerifyAccess(); m_focusObserver = std::move(observer);
    }

    bool InputManager::MoveFocus(FocusNavigationDirection direction, bool wrap)
    {
        VerifyAccess();
        if (!m_active || m_resetting) { return false; }
        auto root = Resolve(m_root);
        if (!root) { return false; }
        if (direction < FocusNavigationDirection::Next || direction > FocusNavigationDirection::Last)
        { throw winrt::hresult_invalid_argument(); }
        auto focused = FocusedElement();
        auto scope = root;
        if (direction == FocusNavigationDirection::Next || direction == FocusNavigationDirection::Previous)
        {
            for (auto ancestor = focused; ancestor; ancestor = ancestor->Parent())
            {
                if (ancestor->GetValue(UIElement::TabFocusNavigationProperty) == KeyboardNavigationMode::Cycle)
                { scope = ancestor; wrap = true; break; }
            }
        }
        struct Candidate { winrt::com_ptr<UIElement> target, group; int index; };
        std::vector<Candidate> candidates;
        auto inside = [](UIElement const* element, UIElement const* parent)
        {
            for (auto current = element ? const_cast<UIElement*>(element)->get_strong() : nullptr; current; current = current->Parent())
            { if (current.get() == parent) { return true; } }
            return false;
        };
        std::function<void(UIElement&, bool)> visit = [&](UIElement& element, bool groupRoot)
        {
            if (!CanReceiveInput(element)) { return; }
            if (!groupRoot && element.GetValue(UIElement::TabFocusNavigationProperty) == KeyboardNavigationMode::Once)
            {
                auto begin = candidates.size();
                visit(element, true);
                if (candidates.size() == begin) { return; }
                std::stable_sort(candidates.begin() + begin, candidates.end(), [](auto const& left, auto const& right)
                    { return left.index < right.index; });
                auto selected = candidates[direction == FocusNavigationDirection::Previous || direction == FocusNavigationDirection::Last ? candidates.size() - 1 : begin].target;
                auto remembered = Resolve(element.m_lastTabFocus);
                if (remembered && CanReceiveInput(*remembered) && remembered->IsKeyboardFocusable() && inside(remembered.get(), &element))
                { selected = remembered; }
                candidates.resize(begin);
                candidates.push_back({ std::move(selected), element.get_strong(), element.KeyboardTabIndex() });
                return;
            }
            if (element.IsKeyboardFocusable()) { candidates.push_back({ element.get_strong(), {}, element.KeyboardTabIndex() }); }
            for (std::size_t i = 0; i < element.VisualChildrenCount(); ++i) { visit(*element.VisualChildAt(i), false); }
        };
        visit(*scope, true);
        std::stable_sort(candidates.begin(), candidates.end(), [](auto const& left, auto const& right)
            { return left.index < right.index; });
        if (candidates.empty()) { return false; }
        if (direction == FocusNavigationDirection::First) { return Focus(candidates.front().target.get()); }
        if (direction == FocusNavigationDirection::Last) { return Focus(candidates.back().target.get()); }
        auto found = std::ranges::find_if(candidates, [&](auto const& item)
            { return item.target == focused || (item.group && inside(focused.get(), item.group.get())); });
        if (found == candidates.end())
        {
            return Focus((direction == FocusNavigationDirection::Previous ? candidates.back() : candidates.front()).target.get());
        }
        if (direction == FocusNavigationDirection::Previous)
        {
            if (found == candidates.begin()) { return wrap && Focus(candidates.back().target.get()); }
            return Focus(std::prev(found)->target.get());
        }
        if (std::next(found) == candidates.end()) { return wrap && Focus(candidates.front().target.get()); }
        return Focus(std::next(found)->target.get());
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
    void InputManager::ReleasePointer(UIElement& element, std::uint32_t pointerId)
    {
        VerifyAccess();
        auto found = m_capture.find(pointerId);
        if (found != m_capture.end() && Resolve(found->second).get() == &element) { ReleasePointer(pointerId); }
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
        if (args.event == InputEvent::KeyDown || args.event == InputEvent::KeyUp || args.event == InputEvent::CharacterReceived)
        {
            auto focused = FocusedElement();
            if (focused && Raise(*focused, args)) { return true; }
            if (m_active && args.event == InputEvent::KeyDown && args.virtualKey == 9 && (args.modifiers & 6) == 0)
            {
                return MoveFocus((args.modifiers & 1) ? FocusNavigationDirection::Previous : FocusNavigationDirection::Next, true);
            }
            return false;
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
        try { auto observer = m_focusObserver; if (observer) { observer(nullptr); } }
        catch (...) { if (!failure) { failure = std::current_exception(); } }
        if (failure) { std::rethrow_exception(failure); }
    }

    void InputManager::Shutdown()
    {
        VerifyAccess();
        if (!m_active) { return; }
        m_active = false;
        m_focusRequest = {};
        CancelInput();
        m_focusObserver = {};
    }
}
