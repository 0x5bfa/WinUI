module Xaml.Runtime;

import std;
import winrt_base;

namespace xaml::runtime
{
    namespace
    {
        void ValidateSize(Size size, bool allowInfinity)
        {
            auto valid = [allowInfinity](double value)
            {
                return value >= 0 && (allowInfinity || std::isfinite(value));
            };
            if (!valid(size.width) || !valid(size.height))
            {
                throw winrt::hresult_invalid_argument();
            }
        }

        struct ActivePass
        {
            explicit ActivePass(bool& active) : m_active(active)
            {
                if (m_active)
                {
                    throw winrt::hresult_illegal_method_call();
                }
                m_active = true;
            }
            ~ActivePass() { m_active = false; }
            bool& m_active;
        };
    }

    DependencyProperty<bool> const UIElement::IsEnabledProperty =
        DependencyProperty<bool>::Register<UIElement>("IsEnabled", true, PropertyEffects::Render);

    bool UIElement::IsEnabled() const
    {
        auto const parent = Parent();
        return GetValue(IsEnabledProperty) && (!parent || parent->IsEnabled());
    }

    void UIElement::IsEnabled(bool value) { SetValue(IsEnabledProperty, value); }

    winrt::com_ptr<UIElement> UIElement::Parent() const
    {
        VerifyAccess();
        auto const parent = m_parent.get();
        winrt::com_ptr<UIElement> result;
        if (parent)
        {
            // The edge is created only from a native UIElement. Resolve the
            // canonical Object weak source, then recover that known native base.
            result.copy_from(static_cast<UIElement*>(parent.get()));
        }
        return result;
    }

    std::size_t UIElement::VisualChildrenCount() const
    {
        VerifyAccess();
        return m_children.size();
    }

    winrt::com_ptr<UIElement> UIElement::VisualChildAt(std::size_t index) const
    {
        VerifyAccess();
        return m_children.at(index);
    }

    void UIElement::SetSingleVisualChild(UIElement* child)
    {
        if (child)
        {
            ReplaceVisualChildren(std::span<UIElement* const>(&child, 1));
        }
        else
        {
            ReplaceVisualChildren({});
        }
    }

    void UIElement::ReplaceVisualChildren(std::span<UIElement* const> children)
    {
        VerifyAccess();
        auto lifetime = get_strong();
        if (m_children.size() == children.size() &&
            std::ranges::equal(m_children, children, {}, &winrt::com_ptr<UIElement>::get))
        {
            return;
        }
        std::vector<winrt::com_ptr<UIElement>> replacement;
        replacement.reserve(children.size());
        for (auto* child : children)
        {
            if (!child || std::ranges::any_of(replacement, [child](auto const& entry) { return entry.get() == child; }))
            {
                throw winrt::hresult_invalid_argument();
            }
            child->VerifyAccess();
            if (auto parent = child->Parent(); parent && parent.get() != this)
            {
                throw winrt::hresult_invalid_argument();
            }
            for (auto ancestor = get_strong(); ancestor;)
            {
                if (ancestor.get() == child)
                {
                    throw winrt::hresult_invalid_argument();
                }
                ancestor = ancestor->Parent();
            }
            winrt::com_ptr<UIElement> owned;
            owned.copy_from(child);
            replacement.push_back(std::move(owned));
        }

        // All fallible preparation precedes changing either side of the edge.
        auto const current = replacement;
        auto const parentReference = Object::get_weak();
        auto previous = std::move(m_children);
        for (auto const& old : previous)
        {
            if (std::ranges::none_of(current, [&](auto const& entry) { return entry.get() == old.get(); }))
            {
                old->m_parent = nullptr;
            }
        }
        m_children = std::move(replacement);
        for (auto const& added : current)
        {
            if (std::ranges::none_of(previous, [&](auto const& entry) { return entry.get() == added.get(); }))
            {
                added->m_parent = parentReference;
            }
        }
        InvalidateMeasure();
        InvalidateRender();

        for (auto const& old : previous)
        {
            if (std::ranges::none_of(current, [&](auto const& entry) { return entry.get() == old.get(); }) &&
                !old->Parent())
            {
                old->NotifyIsEnabledChanged();
                if (!old->Parent())
                {
                    old->OnVisualParentChanged();
                    if (!old->Parent()) { old->NotifyDescendantsOfVisualAncestryChange(); }
                }
            }
        }
        for (auto const& added : current)
        {
            if (std::ranges::none_of(previous, [&](auto const& entry) { return entry.get() == added.get(); }) &&
                added->Parent().get() == this)
            {
                added->NotifyIsEnabledChanged();
                if (added->Parent().get() == this)
                {
                    added->OnVisualParentChanged();
                    if (added->Parent().get() == this) { added->NotifyDescendantsOfVisualAncestryChange(); }
                }
            }
        }
    }

    void UIElement::NotifyIsEnabledChanged()
    {
        auto lifetime = get_strong();
        auto const children = m_children;
        InvalidateRender();
        OnIsEnabledChanged();
        for (auto const& child : children)
        {
            if (child->Parent().get() == this)
            {
                child->NotifyIsEnabledChanged();
            }
        }
    }

    void UIElement::NotifyVisualAncestryChanged()
    {
        auto lifetime = get_strong();
        OnVisualAncestryChanged();
        NotifyDescendantsOfVisualAncestryChange();
    }

    void UIElement::NotifyDescendantsOfVisualAncestryChange()
    {
        auto lifetime = get_strong();
        auto const children = m_children;
        for (auto const& child : children)
        {
            if (child->Parent().get() == this)
            {
                child->NotifyVisualAncestryChanged();
            }
        }
    }

    void UIElement::OnPropertyChanged(PropertyChange const& change)
    {
        if (HasEffect(change.effects, PropertyEffects::Measure)) { InvalidateMeasure(); }
        if (HasEffect(change.effects, PropertyEffects::Arrange)) { InvalidateArrange(); }
        if (HasEffect(change.effects, PropertyEffects::Render)) { InvalidateRender(); }
        if (change.property == IsEnabledProperty.Id()) { NotifyIsEnabledChanged(); }
    }

    void UIElement::InvalidateMeasure()
    {
        VerifyAccess();
        ++m_measureVersion;
        ++m_arrangeVersion;
        ++m_renderVersion;
        m_measureDirty = true;
        m_arrangeDirty = true;
        if (auto parent = Parent()) { parent->InvalidateMeasure(); }
    }

    void UIElement::InvalidateArrange()
    {
        VerifyAccess();
        ++m_arrangeVersion;
        ++m_renderVersion;
        m_arrangeDirty = true;
        if (auto parent = Parent()) { parent->InvalidateArrange(); }
    }

    void UIElement::InvalidateRender()
    {
        VerifyAccess();
        ++m_renderVersion;
        if (auto parent = Parent()) { parent->InvalidateRender(); }
    }

    void UIElement::Measure(Size available)
    {
        VerifyAccess();
        ValidateSize(available, true);
        if (!m_measureDirty && available == m_available) { return; }
        auto lifetime = get_strong();
        ActivePass pass(m_measuring);
        m_measureDirty = true;
        InvalidateArrange();
        auto const version = m_measureVersion;
        auto const desired = MeasureCore(available);
        ValidateSize(desired, false);
        m_available = available;
        m_desired = desired;
        m_measureDirty = version != m_measureVersion;
    }

    void UIElement::Arrange(Rect slot)
    {
        VerifyAccess();
        auto lifetime = get_strong();
        ValidateSize({ slot.width, slot.height }, false);
        if (!std::isfinite(slot.x) || !std::isfinite(slot.y)) { throw winrt::hresult_invalid_argument(); }
        if (m_measureDirty) { Measure({ slot.width, slot.height }); }
        if (!m_arrangeDirty && slot == m_slot) { return; }
        ActivePass pass(m_arranging);
        m_arrangeDirty = true;
        auto const version = m_arrangeVersion;
        auto const actual = ArrangeCore({ slot.width, slot.height });
        ValidateSize(actual, false);
        m_actual = actual;
        m_slot = slot;
        m_arrangeDirty = version != m_arrangeVersion;
        InvalidateRender();
    }

    Size UIElement::DesiredSize() const { VerifyAccess(); return m_desired; }
    Size UIElement::ActualSize() const { VerifyAccess(); return m_actual; }
    Rect UIElement::LayoutSlot() const { VerifyAccess(); return m_slot; }
    bool UIElement::IsMeasureValid() const { VerifyAccess(); return !m_measureDirty; }
    bool UIElement::IsArrangeValid() const { VerifyAccess(); return !m_arrangeDirty; }
    std::uint64_t UIElement::RenderVersion() const { VerifyAccess(); return m_renderVersion; }
    Size UIElement::MeasureCore(Size) { return {}; }
    Size UIElement::ArrangeCore(Size finalSize) { return finalSize; }

}
