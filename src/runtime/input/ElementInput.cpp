module Xaml.Runtime;

import std;
import winrt_base;

namespace xaml::runtime
{
    Point UIElement::PointFromRoot(Point position) const
    {
        VerifyAccess();
        if (!std::isfinite(position.x) || !std::isfinite(position.y))
        {
            throw winrt::hresult_invalid_argument();
        }
        std::vector<winrt::com_ptr<UIElement>> path;
        auto current = const_cast<UIElement*>(this)->get_strong();
        while (current)
        {
            path.push_back(current);
            current = current->Parent();
        }
        for (auto it = path.rbegin(); it != path.rend(); ++it)
        {
            auto const bounds = (*it)->LayoutBounds();
            position.x -= bounds.x;
            position.y -= bounds.y;
            auto parent = (*it)->Parent();
            if ((*it)->IsRightToLeft() != (parent && parent->IsRightToLeft()))
            {
                position.x = bounds.width - position.x;
            }
        }
        return position;
    }

    std::int64_t UIElement::AddInputHandler(InputEvent event, InputHandler handler, bool handledEventsToo)
    {
        VerifyAccess();
        if (!handler) { throw winrt::hresult_invalid_argument(); }
        auto const token = ++m_nextInputToken;
        m_inputSubscriptions.push_back(std::make_shared<InputSubscription>(
            InputSubscription{ token, event, std::move(handler), handledEventsToo }));
        return token;
    }

    void UIElement::RemoveInputHandler(std::int64_t token)
    {
        VerifyAccess();
        for (auto const& entry : m_inputSubscriptions)
        {
            if (entry->token == token) { entry->active = false; }
        }
        std::erase_if(m_inputSubscriptions, [](auto const& entry) { return !entry->active; });
    }

    void UIElement::RaiseInputEvent(InputEventArgs& args)
    {
        VerifyAccess();
        auto lifetime = get_strong();
        auto const subscriptions = m_inputSubscriptions;
        if (!args.handled) { OnInputEvent(args); }
        for (auto const& entry : subscriptions)
        {
            if (entry->active && entry->event == args.event && (!args.handled || entry->handledEventsToo))
            {
                entry->handler(*this, args);
            }
        }
    }
}
