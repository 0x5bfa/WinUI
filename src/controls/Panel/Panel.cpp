module Xaml.Controls;

import std;
import winrt_base;
import Xaml.Runtime;

namespace xaml::controls
{
    std::size_t Panel::ChildrenCount() const { return VisualChildrenCount(); }
    winrt::com_ptr<runtime::UIElement> Panel::ChildAt(std::size_t index) const { return VisualChildAt(index); }

    std::vector<winrt::com_ptr<runtime::UIElement>> Panel::SnapshotChildren() const
    {
        std::vector<winrt::com_ptr<runtime::UIElement>> children;
        auto const count = ChildrenCount();
        children.reserve(count);
        for (std::size_t index = 0; index < count; ++index) { children.push_back(ChildAt(index)); }
        return children;
    }

    void Panel::Append(runtime::UIElement* child) { Insert(ChildrenCount(), child); }

    void Panel::Insert(std::size_t index, runtime::UIElement* child)
    {
        VerifyAccess();
        auto const snapshot = SnapshotChildren();
        if (index > snapshot.size()) { throw winrt::hresult_out_of_bounds(); }
        std::vector<runtime::UIElement*> next;
        next.reserve(snapshot.size() + 1);
        for (auto const& element : snapshot) { next.push_back(element.get()); }
        next.insert(next.begin() + static_cast<std::ptrdiff_t>(index), child);
        ReplaceVisualChildren(next);
    }

    void Panel::RemoveAt(std::size_t index)
    {
        VerifyAccess();
        auto const snapshot = SnapshotChildren();
        if (index >= snapshot.size()) { throw winrt::hresult_out_of_bounds(); }
        std::vector<runtime::UIElement*> next;
        next.reserve(snapshot.size() - 1);
        for (std::size_t i = 0; i < snapshot.size(); ++i)
        {
            if (i != index) { next.push_back(snapshot[i].get()); }
        }
        ReplaceVisualChildren(next);
    }

    void Panel::Move(std::size_t from, std::size_t to)
    {
        VerifyAccess();
        auto const snapshot = SnapshotChildren();
        if (from >= snapshot.size() || to >= snapshot.size()) { throw winrt::hresult_out_of_bounds(); }
        if (from == to) { return; }
        std::vector<runtime::UIElement*> next;
        next.reserve(snapshot.size());
        for (auto const& element : snapshot) { next.push_back(element.get()); }
        if (from < to)
        {
            std::rotate(next.begin() + static_cast<std::ptrdiff_t>(from),
                next.begin() + static_cast<std::ptrdiff_t>(from + 1),
                next.begin() + static_cast<std::ptrdiff_t>(to + 1));
        }
        else
        {
            std::rotate(next.begin() + static_cast<std::ptrdiff_t>(to),
                next.begin() + static_cast<std::ptrdiff_t>(from),
                next.begin() + static_cast<std::ptrdiff_t>(from + 1));
        }
        ReplaceVisualChildren(next);
    }

    void Panel::Clear() { ReplaceVisualChildren({}); }

}
