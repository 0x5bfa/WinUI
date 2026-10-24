module Xaml.Runtime;

import std;
import winrt_base;

namespace xaml::runtime
{
    namespace
    {
        std::atomic<std::uint64_t> nextRenderIdentity{};
        std::uint64_t AllocateRenderIdentity()
        {
            auto current = nextRenderIdentity.load(std::memory_order_relaxed);
            for (;;)
            {
                if (current == std::numeric_limits<std::uint64_t>::max()) { throw winrt::hresult_out_of_bounds(); }
                if (nextRenderIdentity.compare_exchange_weak(current, current + 1, std::memory_order_relaxed)) { return current + 1; }
            }
        }
    }
    UIElement::UIElement() : m_renderIdentity(AllocateRenderIdentity()) {}
    DependencyProperty<KeyboardNavigationMode> const UIElement::TabFocusNavigationProperty =
        DependencyProperty<KeyboardNavigationMode>::Register<UIElement>("TabFocusNavigation", KeyboardNavigationMode::Local,
            PropertyEffects::None, +[](KeyboardNavigationMode value)
            { return value >= KeyboardNavigationMode::Local && value <= KeyboardNavigationMode::Once; });
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

        double PixelRound(double value, double scale)
        {
            // Extremely large finite input must not overflow during scaling.
            auto const pixels = value * scale;
            return std::isfinite(pixels) ? std::round(pixels) / scale : value;
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

    DependencyProperty<bool> const UIElement::IsHitTestVisibleProperty =
        DependencyProperty<bool>::Register<UIElement>("IsHitTestVisible", true);

    DependencyProperty<Visibility> const UIElement::VisibilityProperty =
        DependencyProperty<Visibility>::Register<UIElement>("Visibility", Visibility::Visible,
            PropertyEffects::Measure | PropertyEffects::Render,
            +[](Visibility value) { return value == Visibility::Visible || value == Visibility::Collapsed; });

    DependencyProperty<double> const UIElement::OpacityProperty =
        DependencyProperty<double>::Register<UIElement>("Opacity", 1.0, PropertyEffects::Render,
            +[](double value) { return std::isfinite(value) && value >= 0.0 && value <= 1.0; });

    DependencyProperty<bool> const UIElement::UseLayoutRoundingProperty =
        DependencyProperty<bool>::Register<UIElement>("UseLayoutRounding", true,
            PropertyEffects::Arrange | PropertyEffects::Render);

    bool UIElement::IsEnabled() const
    {
        auto const parent = Parent();
        return GetValue(IsEnabledProperty) && IsEnabledCore() && (!parent || parent->IsEnabled());
    }

    void UIElement::IsEnabled(bool value) { SetValue(IsEnabledProperty, value); }
    void UIElement::InvalidateIsEnabled() { VerifyAccess(); NotifyIsEnabledChanged(); }

    bool UIElement::IsHitTestVisible() const
    {
        auto const enabled = GetValue(IsHitTestVisibleProperty);
        if (!enabled) { return false; }
        auto const parent = Parent();
        return !parent || parent->IsHitTestVisible();
    }

    void UIElement::IsHitTestVisible(bool value) { SetValue(IsHitTestVisibleProperty, value); }

    Visibility UIElement::VisibilityState() const { return GetValue(VisibilityProperty); }
    void UIElement::VisibilityState(Visibility value) { SetValue(VisibilityProperty, value); }
    double UIElement::Opacity() const { return GetValue(OpacityProperty); }
    void UIElement::Opacity(double value) { SetValue(OpacityProperty, value); }
    bool UIElement::UseLayoutRounding() const
    {
        auto const property = UseLayoutRoundingProperty.Erase();
        if (HasValueOverride(property)) { return GetValue(UseLayoutRoundingProperty); }
        if (auto parent = Parent()) { return parent->UseLayoutRounding(); }
        return GetValue(UseLayoutRoundingProperty);
    }
    void UIElement::UseLayoutRounding(bool value) { SetValue(UseLayoutRoundingProperty, value); }

    bool UIElement::IsRightToLeft() const
    {
        VerifyAccess();
        if (auto parent = Parent()) { return parent->IsRightToLeft(); }
        return false;
    }

    std::optional<Rect> UIElement::ClipBounds() const
    {
        VerifyAccess();
        return m_clipBounds;
    }

    void UIElement::ClipBounds(std::optional<Rect> value)
    {
        VerifyAccess();
        if (value && (!std::isfinite(value->x) || !std::isfinite(value->y) ||
            !std::isfinite(value->width) || !std::isfinite(value->height) ||
            value->width < 0 || value->height < 0))
        {
            throw winrt::hresult_invalid_argument();
        }
        if (m_clipBounds == value) { return; }
        m_clipBounds = value;
        InvalidateRender();
    }

    winrt::com_ptr<UIElement> UIElement::Parent() const
    {
        VerifyAccess();
        auto const parent = m_parent.get();
        winrt::com_ptr<UIElement> result;
        if (parent)
        {
            // The edge is created only from a native UIElement. Resolve the
            // canonical DependencyObject weak source, then recover that known native base.
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
            if (child->m_rootInvalidated)
            {
                throw winrt::hresult_invalid_argument();
            }
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
        struct InheritedSnapshot
        {
            winrt::com_ptr<UIElement> element;
            DependencyPropertyHandle property;
            std::any before;
        };
        std::vector<InheritedSnapshot> inherited;
        auto snapshot = [&](UIElement& branch)
        {
            for (auto const& property : DependencyPropertyHandle::InheritableProperties())
            {
                std::function<void(UIElement&)> visit = [&](UIElement& node)
                {
                    if (property.IsValidFor(node))
                    {
                        if (!node.HasValueOverride(property))
                        {
                            inherited.push_back({ node.get_strong(), property, node.GetValue(property) });
                        }
                        return;
                    }
                    for (auto const& child : node.m_children) { visit(*child); }
                };
                visit(branch);
            }
        };
        for (auto const& old : m_children)
        {
            if (std::ranges::none_of(current, [&](auto const& entry) { return entry == old; })) { snapshot(*old); }
        }
        for (auto const& added : current)
        {
            if (std::ranges::none_of(m_children, [&](auto const& entry) { return entry == added; })) { snapshot(*added); }
        }
        auto const parentReference = DependencyObject::get_weak();
        auto previous = std::move(m_children);
        for (auto const& old : previous)
        {
            if (std::ranges::none_of(current, [&](auto const& entry) { return entry.get() == old.get(); }))
            {
                old->m_parent = nullptr;
                old->UpdateRootConnection(false);
                old->SetLayoutScale(1.0);
                old->MarkLayoutRoundingSubtree();
            }
        }
        m_children = std::move(replacement);
        for (auto const& added : current)
        {
            if (std::ranges::none_of(previous, [&](auto const& entry) { return entry.get() == added.get(); }))
            {
                added->m_parent = parentReference;
                added->SetLayoutScale(m_layoutScale);
                added->MarkLayoutRoundingSubtree();
                added->UpdateRootConnection(m_connected, m_deferredEnqueue);
            }
        }
        InvalidateMeasure();
        InvalidateRender();

        for (auto const& entry : inherited) { entry.element->NotifyInheritedValueChanged(entry.property, entry.before); }

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
        if (change.property == IsHitTestVisibleProperty.Id()) { InvalidateRender(); }
        if (change.property == UseLayoutRoundingProperty.Id())
        {
            // Layout rounding follows the visual parent unless explicitly overridden.
            // Child arrangements must be revisited even if the root slot is unchanged.
            for (auto const& child : m_children) { child->MarkLayoutRoundingSubtree(); }
        }
    }

    std::optional<std::any> UIElement::GetInheritedValue(DependencyPropertyHandle const& property) const
    {
        for (auto ancestor = Parent(); ancestor; ancestor = ancestor->Parent())
        {
            if (property.IsValidFor(*ancestor)) { return ancestor->GetValue(property); }
        }
        return {};
    }

    void UIElement::OnInheritedPropertyChanged(DependencyPropertyHandle const& property,
        std::any const& before, std::any const&)
    {
        auto children = m_children;
        for (auto const& child : children)
        {
            if (child->Parent().get() != this) { continue; }
            if (property.IsValidFor(*child)) { child->NotifyInheritedValueChanged(property, before); }
            else { child->OnInheritedPropertyChanged(property, before, {}); }
        }
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
        else { RequestRootUpdate(); }
    }

    void UIElement::InvalidateArrange()
    {
        VerifyAccess();
        ++m_arrangeVersion;
        ++m_renderVersion;
        m_arrangeDirty = true;
        if (auto parent = Parent()) { parent->InvalidateArrange(); }
        else { RequestRootUpdate(); }
    }

    void UIElement::InvalidateRender()
    {
        VerifyAccess();
        ++m_renderVersion;
        if (auto parent = Parent()) { parent->InvalidateRender(); }
        else { RequestRootUpdate(); }
    }

    void UIElement::SetRootInvalidationCallback(std::function<void()> callback, DeferredEnqueue deferred,
        std::function<void()> updateLayout)
    {
        VerifyAccess();
        auto lifetime = get_strong();
        bool const connected = static_cast<bool>(callback);
        m_rootInvalidated = std::move(callback);
        m_rootUpdateLayout = std::move(updateLayout);
        UpdateRootConnection(connected, deferred);
    }

    void UIElement::UpdateRootConnection(bool connected, DeferredEnqueue const& deferred)
    {
        VerifyAccess();
        auto lifetime = get_strong();
        if (m_connected == connected) { return; }
        m_connected = connected;
        auto pendingEnqueue = m_deferredEnqueue;
        if (connected) { m_deferredEnqueue = deferred; }
        if (!connected && m_loaded)
        {
            m_loaded = false;
            // Unloaded is posted through the session dispatcher. Keep the
            // detached element alive until its event has been delivered.
            bool queued{};
            if (pendingEnqueue)
            {
                try
                {
                    queued = pendingEnqueue([lifetime]
                    {
                        lifetime->OnUnloaded();
                        lifetime->RaiseLifecycleEvent(LifecycleEvent::Unloaded);
                    });
                }
                catch (...) { /* Dispatcher shutdown can reject the event. */ }
            }
            if (!queued)
            {
                OnUnloaded();
                RaiseLifecycleEvent(LifecycleEvent::Unloaded);
            }
        }
        auto const children = m_children;
        for (auto const& child : children)
        {
            if (child->Parent().get() == this && child->m_connected != m_connected)
            {
                child->UpdateRootConnection(m_connected, m_deferredEnqueue);
            }
        }
        if (!connected) { m_deferredEnqueue = {}; }
    }

    void UIElement::DispatchLoaded()
    {
        VerifyAccess();
        auto lifetime = get_strong();
        if (!m_connected) { return; }
        if (!m_loaded)
        {
            m_loaded = true;
            OnLoaded();
            RaiseLifecycleEvent(LifecycleEvent::Loaded);
        }
        auto const children = m_children;
        for (auto const& child : children)
        {
            if (m_connected && child->Parent().get() == this)
            {
                child->DispatchLoaded();
            }
        }
    }

    void UIElement::RequestRootUpdate()
    {
        auto callback = m_rootInvalidated;
        if (callback) { callback(); }
    }

    void UIElement::UpdateLayout()
    {
        VerifyAccess();
        auto root = get_strong();
        while (auto parent = root->Parent()) { root = std::move(parent); }
        auto callback = root->m_rootUpdateLayout;
        if (callback) { callback(); }
    }

    void UIElement::Measure(Size available)
    {
        VerifyAccess();
        ValidateSize(available, true);
        if (!m_measureDirty && available == m_available) { return; }
        auto lifetime = get_strong();
        ActivePass pass(m_measuring);
        // XAML Loading is raised by the first Measure, even when this element
        // has not yet been attached to a live content root.
        if (!m_loadingRaised)
        {
            m_loadingRaised = true;
            try { OnLoading(); }
            catch (...)
            {
                m_loadingRaised = false;
                throw;
            }
            try { RaiseLifecycleEvent(LifecycleEvent::Loading); }
            catch (...)
            {
                m_loadingRaised = false;
                throw;
            }
        }
        m_measureDirty = true;
        InvalidateArrange();
        auto const version = m_measureVersion;
        auto const desired = VisibilityState() == Visibility::Collapsed ? Size{} : MeasureCore(available);
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
        if (UseLayoutRounding())
        {
            slot = { PixelRound(slot.x, m_layoutScale), PixelRound(slot.y, m_layoutScale),
                PixelRound(slot.width, m_layoutScale), PixelRound(slot.height, m_layoutScale) };
        }
        if (m_measureDirty) { Measure({ slot.width, slot.height }); }
        if (!m_arrangeDirty && slot == m_slot) { return; }
        ActivePass pass(m_arranging);
        m_arrangeDirty = true;
        auto const version = m_arrangeVersion;
        auto actual = VisibilityState() == Visibility::Collapsed ? Size{} : ArrangeCore({ slot.width, slot.height });
        ValidateSize(actual, false);
        if (UseLayoutRounding())
        {
            actual.width = PixelRound(actual.width, m_layoutScale);
            actual.height = PixelRound(actual.height, m_layoutScale);
        }
        auto const previousSize = m_actual;
        m_actual = actual;
        m_slot = slot;
        m_arrangeDirty = version != m_arrangeVersion;
        InvalidateRender();
        if (previousSize != actual) { OnActualSizeChanged(previousSize, actual); }
    }

    Size UIElement::DesiredSize() const { VerifyAccess(); return m_desired; }
    Size UIElement::ActualSize() const { VerifyAccess(); return m_actual; }
    Rect UIElement::LayoutSlot() const { VerifyAccess(); return m_slot; }
    Rect UIElement::LayoutBounds() const
    {
        VerifyAccess();
        return { m_slot.x + m_offsetX, m_slot.y + m_offsetY, m_actual.width, m_actual.height };
    }

    winrt::com_ptr<UIElement> UIElement::HitTest(Point position) const
    {
        VerifyAccess();
        if (!std::isfinite(position.x) || !std::isfinite(position.y))
        {
            throw winrt::hresult_invalid_argument();
        }
        if (m_arrangeDirty || m_measureDirty || VisibilityState() == Visibility::Collapsed ||
            !IsEnabled() || !IsHitTestVisible())
        {
            return nullptr;
        }

        auto lifetime = const_cast<UIElement*>(this)->get_strong();
        auto const bounds = LayoutBounds();
        Point local{ position.x - bounds.x, position.y - bounds.y };
        auto const parent = Parent();
        bool const parentRTL = parent && parent->IsRightToLeft();
        if (IsRightToLeft() != parentRTL)
        {
            local.x = bounds.width - local.x;
        }
        auto contains = [](Rect area, Point point)
        {
            return point.x >= area.x && point.y >= area.y &&
                point.x < area.x + area.width && point.y < area.y + area.height;
        };
        if (m_clipBounds && !contains(*m_clipBounds, local)) { return nullptr; }

        // Composition paints later siblings above earlier siblings.
        auto const children = m_children;
        for (auto it = children.rbegin(); it != children.rend(); ++it)
        {
            if ((*it)->Parent().get() != this) { continue; }
            if (auto target = (*it)->HitTest(local)) { return target; }
        }
        if (!contains({ 0, 0, bounds.width, bounds.height }, local)) { return nullptr; }
        winrt::com_ptr<UIElement> result;
        result.copy_from(const_cast<UIElement*>(this));
        return result;
    }
    bool UIElement::IsMeasureValid() const { VerifyAccess(); return !m_measureDirty; }
    bool UIElement::IsArrangeValid() const { VerifyAccess(); return !m_arrangeDirty; }
    std::uint64_t UIElement::RenderVersion() const { VerifyAccess(); return m_renderVersion; }
    bool UIElement::IsConnectedToRoot() const { VerifyAccess(); return m_connected; }
    bool UIElement::IsLoaded() const { VerifyAccess(); return m_loaded; }
    std::int64_t UIElement::AddLoadingHandler(LifecycleHandler handler)
    {
        return AddLifecycleHandler(LifecycleEvent::Loading, std::move(handler));
    }
    std::int64_t UIElement::AddLoadedHandler(LifecycleHandler handler)
    {
        return AddLifecycleHandler(LifecycleEvent::Loaded, std::move(handler));
    }
    std::int64_t UIElement::AddUnloadedHandler(LifecycleHandler handler)
    {
        return AddLifecycleHandler(LifecycleEvent::Unloaded, std::move(handler));
    }
    void UIElement::RemoveLoadingHandler(std::int64_t token)
    {
        RemoveLifecycleHandler(LifecycleEvent::Loading, token);
    }
    void UIElement::RemoveLoadedHandler(std::int64_t token)
    {
        RemoveLifecycleHandler(LifecycleEvent::Loaded, token);
    }
    void UIElement::RemoveUnloadedHandler(std::int64_t token)
    {
        RemoveLifecycleHandler(LifecycleEvent::Unloaded, token);
    }

    std::int64_t UIElement::AddLifecycleHandler(LifecycleEvent event, LifecycleHandler handler)
    {
        VerifyAccess();
        auto lifetime = get_strong();
        if (!handler) { throw winrt::hresult_invalid_argument(); }
        if (m_nextLifecycleToken == std::numeric_limits<std::int64_t>::max())
        {
            throw winrt::hresult_illegal_method_call();
        }
        auto const token = m_nextLifecycleToken + 1;
        m_lifecycleSubscriptions.push_back(std::make_shared<LifecycleSubscription>(
            event, token, std::move(handler)));
        m_nextLifecycleToken = token;
        return token;
    }

    void UIElement::RemoveLifecycleHandler(LifecycleEvent event, std::int64_t token)
    {
        VerifyAccess();
        auto lifetime = get_strong();
        auto const found = std::ranges::find_if(m_lifecycleSubscriptions,
            [=](auto const& entry) { return entry->event == event && entry->token == token; });
        if (found == m_lifecycleSubscriptions.end()) { return; }
        auto removed = std::move(*found);
        removed->active = false;
        m_lifecycleSubscriptions.erase(found);
    }

    void UIElement::RaiseLifecycleEvent(LifecycleEvent event)
    {
        VerifyAccess();
        auto lifetime = get_strong();
        auto const snapshot = m_lifecycleSubscriptions;
        for (auto const& entry : snapshot)
        {
            if (entry->active && entry->event == event) { entry->handler(*this); }
        }
    }
    Size UIElement::MeasureCore(Size) { return {}; }
    Size UIElement::ArrangeCore(Size finalSize) { return finalSize; }

    void UIElement::SetLayoutOffset(double x, double y)
    {
        VerifyAccess();
        if (!std::isfinite(x) || !std::isfinite(y)) { throw winrt::hresult_invalid_argument(); }
        if (UseLayoutRounding())
        {
            x = PixelRound(x, m_layoutScale);
            y = PixelRound(y, m_layoutScale);
        }
        m_offsetX = x;
        m_offsetY = y;
    }

    void UIElement::SetLayoutScale(double scale)
    {
        VerifyAccess();
        if (!std::isfinite(scale) || scale <= 0) { throw winrt::hresult_invalid_argument(); }
        if (m_layoutScale == scale) { return; }
        MarkLayoutScaleSubtree(scale);
        if (auto parent = Parent()) { parent->InvalidateMeasure(); }
        else { RequestRootUpdate(); }
    }

    void UIElement::MarkLayoutScaleSubtree(double scale)
    {
        // Updating a large tree must schedule just one root frame. Per-child
        // InvalidateMeasure would walk the ancestors once for every element.
        m_layoutScale = scale;
        ++m_measureVersion;
        ++m_arrangeVersion;
        ++m_renderVersion;
        m_measureDirty = true;
        m_arrangeDirty = true;
        for (auto const& child : m_children)
        {
            child->MarkLayoutScaleSubtree(scale);
        }
    }

    void UIElement::MarkLayoutRoundingSubtree()
    {
        ++m_arrangeVersion;
        ++m_renderVersion;
        m_arrangeDirty = true;
        for (auto const& child : m_children) { child->MarkLayoutRoundingSubtree(); }
    }

    void UIElement::InvalidateInheritedFlowDirection()
    {
        VerifyAccess();
        for (auto const& child : m_children) { child->MarkFlowDirectionSubtree(); }
    }

    void UIElement::MarkFlowDirectionSubtree()
    {
        ++m_arrangeVersion;
        ++m_renderVersion;
        m_arrangeDirty = true;
        for (auto const& child : m_children) { child->MarkFlowDirectionSubtree(); }
    }

}
