module Xaml.Runtime;

import std;
import winrt_base;

namespace xaml::runtime
{
    namespace
    {
        struct UnsetSentinel : winrt::implements<UnsetSentinel, winrt::Windows::Foundation::IInspectable>
        {
        };
    }

    winrt::Windows::Foundation::IInspectable DependencyPropertyHandle::UnsetValue()
    {
        static auto const value = winrt::make<UnsetSentinel>();
        return value;
    }

    DependencyObject::DependencyObject() noexcept : m_thread(std::this_thread::get_id())
    {
    }

    bool DependencyObject::HasThreadAccess() const noexcept
    {
        return m_thread == std::this_thread::get_id();
    }

    winrt::Microsoft::UI::Dispatching::DispatcherQueue DependencyObject::DispatcherQueue() const
    {
        VerifyAccess();
        return winrt::Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread();
    }

    void DependencyObject::VerifyAccess() const
    {
        if (!HasThreadAccess())
        {
            throw winrt::hresult_wrong_thread();
        }
    }

    void DependencyObject::abi_enter() const
    {
        VerifyAccess();
    }

    void DependencyObject::VerifyProperty(DependencyPropertyHandle const& property) const
    {
        VerifyAccess();
        if (!property.m_info || !property.m_info->acceptsOwner(*this))
        {
            throw winrt::hresult_invalid_argument();
        }
    }

    std::any DependencyObject::GetDefaultValue(DependencyPropertyHandle const& property) const
    {
        auto const& info = property.m_info;
        auto const* key = info.get();
        if (!info->createDefault) { return info->defaultValue; }
        if (auto found = m_createdDefaults.find(key); found != m_createdDefaults.end())
        {
            return found->second.value;
        }
        if (!m_creatingDefaults.insert(key).second)
        {
            throw winrt::hresult_illegal_method_call();
        }
        try
        {
            auto lifetime = const_cast<DependencyObject*>(this)->get_strong();
            auto value = info->createDefault(*const_cast<DependencyObject*>(this));
            if (!info->acceptsValue(value)) { throw winrt::hresult_invalid_argument(); }
            m_createdDefaults.emplace(key, Entry{ info, std::move(value) });
            m_creatingDefaults.erase(key);
        }
        catch (...)
        {
            m_creatingDefaults.erase(key);
            throw;
        }
        return m_createdDefaults.at(key).value;
    }

    std::any DependencyObject::GetValue(DependencyPropertyHandle const& property) const
    {
        VerifyProperty(property);
        return EvaluateValue(property, true);
    }

    std::any DependencyObject::GetAnimationBaseValue(DependencyPropertyHandle const& property) const
    {
        VerifyProperty(property);
        return EvaluateValue(property, false);
    }

    std::any DependencyObject::EvaluateValue(DependencyPropertyHandle const& property,
        bool includeAnimation) const
    {
        auto const* key = property.m_info.get();
        if (includeAnimation)
        {
            if (auto found = m_animationValues.find(key); found != m_animationValues.end())
            {
                return found->second.value;
            }
            if (auto found = m_stateValues.find(key); found != m_stateValues.end() && !found->second.empty())
            {
                return found->second.rbegin()->second.value;
            }
        }
        if (auto found = m_values.find(key); found != m_values.end())
        {
            return found->second.value;
        }
        if (auto found = m_templateValues.find(key); found != m_templateValues.end())
        {
            return found->second.value;
        }
        if (auto found = m_styleValues.find(key); found != m_styleValues.end())
        {
            return found->second.value;
        }
        if (property.Inherits())
        {
            if (auto inherited = GetInheritedValue(property)) { return *inherited; }
        }
        return GetDefaultValue(property);
    }

    winrt::Windows::Foundation::IInspectable DependencyObject::GetBoxedValue(
        DependencyPropertyHandle const& property) const
    {
        VerifyProperty(property);
        return property.m_info->box(GetValue(property));
    }

    winrt::Windows::Foundation::IInspectable DependencyObject::ReadLocalBoxedValue(
        DependencyPropertyHandle const& property) const
    {
        auto const local = ReadLocalValue(property);
        return local ? property.m_info->box(*local) : DependencyPropertyHandle::UnsetValue();
    }

    winrt::Windows::Foundation::IInspectable DependencyObject::GetAnimationBaseBoxedValue(
        DependencyPropertyHandle const& property) const
    {
        VerifyProperty(property);
        return property.m_info->box(GetAnimationBaseValue(property));
    }

    std::optional<std::any> DependencyObject::ReadLocalValue(DependencyPropertyHandle const& property) const
    {
        VerifyProperty(property);
        auto const found = m_values.find(property.m_info.get());
        if (found == m_values.end()) { return std::nullopt; }
        return found->second.value;
    }

    bool DependencyObject::HasValueOverride(DependencyPropertyHandle const& property) const
    {
        VerifyProperty(property);
        auto const* key = property.m_info.get();
        return m_values.contains(key) || m_templateValues.contains(key) ||
            m_styleValues.contains(key) || m_animationValues.contains(key) || m_stateValues.contains(key);
    }

    void DependencyObject::SetValue(DependencyPropertyHandle const& property, std::any value)
    {
        ValidateValue(property, value);
        DropExpression(property, false);
        SetLayerValue(property, std::move(value), ValueLayer::Local);
    }

    void DependencyObject::SetStyleValue(DependencyPropertyHandle const& property, std::any value)
    {
        SetLayerValue(property, std::move(value), ValueLayer::Style);
    }

    void DependencyObject::SetAnimationValue(DependencyPropertyHandle const& property, std::any value)
    {
        SetLayerValue(property, std::move(value), ValueLayer::Animation);
    }

    void DependencyObject::SetLayerValue(DependencyPropertyHandle const& property, std::any value,
        ValueLayer layer)
    {
        VerifyProperty(property);
        auto lifetime = get_strong();
        auto const info = property.m_info;
        if (!info->acceptsValue(value)) { throw winrt::hresult_invalid_argument(); }
        // A validator can reenter the property engine. Read the effective
        // value only after validation and pin the owner through notification.
        auto const oldValue = GetValue(property);
        auto& storage = layer == ValueLayer::Local ? m_values :
            layer == ValueLayer::Template ? m_templateValues :
            layer == ValueLayer::Style ? m_styleValues : m_animationValues;
        storage.insert_or_assign(info.get(), Entry{ info, std::move(value) });
        auto const newValue = GetValue(property);
        if (!info->valuesEqual(oldValue, newValue))
        {
            NotifyPropertyChanged(info, { info.get(), info->effects, oldValue, newValue });
        }
    }

    void DependencyObject::SetBoxedValue(DependencyPropertyHandle const& property,
        winrt::Windows::Foundation::IInspectable const& value)
    {
        VerifyProperty(property);
        if (value == DependencyPropertyHandle::UnsetValue())
        {
            throw winrt::hresult_invalid_argument();
        }
        auto lifetime = get_strong();
        SetValue(property, property.m_info->unbox(value));
    }

    void DependencyObject::ClearValue(DependencyPropertyHandle const& property)
    {
        DropExpression(property, false);
        ClearLayerValue(property, ValueLayer::Local);
    }

    void DependencyObject::ClearStyleValue(DependencyPropertyHandle const& property)
    {
        ClearLayerValue(property, ValueLayer::Style);
    }

    void DependencyObject::ClearAnimationValue(DependencyPropertyHandle const& property)
    {
        ClearLayerValue(property, ValueLayer::Animation);
    }

    void DependencyObject::ClearLayerValue(DependencyPropertyHandle const& property, ValueLayer layer)
    {
        VerifyProperty(property);
        auto lifetime = get_strong();
        auto const info = property.m_info;
        auto& storage = layer == ValueLayer::Local ? m_values :
            layer == ValueLayer::Template ? m_templateValues :
            layer == ValueLayer::Style ? m_styleValues : m_animationValues;
        if (!storage.contains(info.get())) { return; }
        auto const oldValue = GetValue(property);
        // Resolve a lazy default only if no other layer can provide the next
        // effective value. A failing factory must preserve the existing layer.
        if ((layer == ValueLayer::Local || !m_values.contains(info.get())) &&
            (layer == ValueLayer::Template || !m_templateValues.contains(info.get())) &&
            (layer == ValueLayer::Style || !m_styleValues.contains(info.get())) &&
            !m_stateValues.contains(info.get()) &&
            (layer == ValueLayer::Animation || !m_animationValues.contains(info.get())))
        {
            if (!property.Inherits() || !GetInheritedValue(property)) { (void)GetDefaultValue(property); }
        }
        storage.erase(info.get());
        auto const newValue = GetValue(property);
        if (!info->valuesEqual(oldValue, newValue))
        {
            NotifyPropertyChanged(info, { info.get(), info->effects, oldValue, newValue });
        }
    }

    void DependencyObject::ValidateValue(DependencyPropertyHandle const& property, std::any const& value) const
    {
        VerifyProperty(property);
        if (!property.m_info->acceptsValue(value)) { throw winrt::hresult_invalid_argument(); }
    }

    void DependencyObject::SetCurrentValue(DependencyPropertyHandle const& property, std::any value)
    {
        VerifyProperty(property);
        auto found = m_localExpressions.find(property.m_info.get());
        if (found != m_localExpressions.end()) { SetExpressionValue(property, found->second.token, std::move(value)); }
        else { SetValue(property, std::move(value)); }
    }

    void DependencyObject::SetStateValue(DependencyPropertyHandle const& property,
        std::uint64_t contributor, std::any value)
    {
        ValidateValue(property, value);
        if (!contributor) { throw winrt::hresult_invalid_argument(); }
        auto lifetime = get_strong();
        auto before = GetValue(property);
        m_stateValues[property.m_info.get()].insert_or_assign(contributor, Entry{ property.m_info, std::move(value) });
        auto after = GetValue(property);
        if (!property.m_info->valuesEqual(before, after))
        {
            NotifyPropertyChanged(property.m_info, { property.Id(), property.m_info->effects, before, after });
        }
    }

    void DependencyObject::ClearStateValue(DependencyPropertyHandle const& property, std::uint64_t contributor)
    {
        VerifyProperty(property);
        auto found = m_stateValues.find(property.m_info.get());
        if (found == m_stateValues.end() || !found->second.contains(contributor)) { return; }
        auto lifetime = get_strong();
        auto before = GetValue(property);
        if (found->second.size() == 1 && !m_animationValues.contains(property.m_info.get()))
        { (void)EvaluateValue(property, false); }
        found = m_stateValues.find(property.m_info.get());
        if (found == m_stateValues.end()) { return; }
        found->second.erase(contributor);
        if (found->second.empty()) { m_stateValues.erase(found); }
        auto after = GetValue(property);
        if (!property.m_info->valuesEqual(before, after))
        {
            NotifyPropertyChanged(property.m_info, { property.Id(), property.m_info->effects, before, after });
        }
    }

    void DependencyObject::ClearExpressionValue(DependencyPropertyHandle const& property,
        std::uint64_t token, bool templateValue)
    {
        if (IsExpressionCurrent(property, token, templateValue))
        {
            ClearLayerValue(property, templateValue ? ValueLayer::Template : ValueLayer::Local);
        }
    }

    void DependencyObject::SetTemplateValue(DependencyPropertyHandle const& property, std::any value)
    {
        SetLayerValue(property, std::move(value), ValueLayer::Template);
    }

    void DependencyObject::ClearTemplateValue(DependencyPropertyHandle const& property)
    {
        DropExpression(property, true);
        ClearLayerValue(property, ValueLayer::Template);
    }

    void DependencyObject::DropExpression(DependencyPropertyHandle const& property, bool templateValue)
    {
        VerifyProperty(property);
        auto lifetime = get_strong();
        auto& expressions = templateValue ? m_templateExpressions : m_localExpressions;
        auto found = expressions.find(property.m_info.get());
        if (found == expressions.end()) { return; }
        auto cleanup = std::move(found->second.disconnect);
        expressions.erase(found);
        if (cleanup) { cleanup(); }
    }

    std::uint64_t DependencyObject::AttachExpression(DependencyPropertyHandle const& property,
        std::function<void()> disconnect, bool templateValue)
    {
        VerifyProperty(property);
        if (!disconnect || m_nextExpression == std::numeric_limits<std::uint64_t>::max())
        {
            throw winrt::hresult_invalid_argument();
        }
        auto lifetime = get_strong();
        DropExpression(property, templateValue);
        auto const token = ++m_nextExpression;
        auto& expressions = templateValue ? m_templateExpressions : m_localExpressions;
        expressions.insert_or_assign(property.m_info.get(), Expression{ property.m_info, token, std::move(disconnect) });
        return token;
    }

    bool DependencyObject::IsExpressionCurrent(DependencyPropertyHandle const& property,
        std::uint64_t token, bool templateValue) const
    {
        VerifyProperty(property);
        auto const& expressions = templateValue ? m_templateExpressions : m_localExpressions;
        auto found = expressions.find(property.m_info.get());
        return found != expressions.end() && found->second.token == token;
    }

    void DependencyObject::SetExpressionValue(DependencyPropertyHandle const& property, std::uint64_t token,
        std::any value, bool templateValue)
    {
        if (!IsExpressionCurrent(property, token, templateValue)) { return; }
        SetLayerValue(property, std::move(value), templateValue ? ValueLayer::Template : ValueLayer::Local);
    }

    void DependencyObject::DetachExpression(DependencyPropertyHandle const& property,
        std::uint64_t token, bool templateValue)
    {
        if (!IsExpressionCurrent(property, token, templateValue)) { return; }
        auto lifetime = get_strong();
        DropExpression(property, templateValue);
        auto const& expressions = templateValue ? m_templateExpressions : m_localExpressions;
        if (!expressions.contains(property.m_info.get()))
        {
            ClearLayerValue(property, templateValue ? ValueLayer::Template : ValueLayer::Local);
        }
    }

    std::int64_t DependencyObject::RegisterPropertyChangedCallback(
        DependencyPropertyHandle const& property, PropertyChangedHandler handler)
    {
        VerifyProperty(property);
        if (!handler) { throw winrt::hresult_invalid_argument(); }
        return AddPropertyChangedCallback(property.m_info, std::move(handler));
    }

    void DependencyObject::UnregisterPropertyChangedCallback(DependencyPropertyHandle const& property,
        std::int64_t token)
    {
        VerifyProperty(property);
        RemovePropertyChangedCallback(property.m_info.get(), token);
    }

    std::int64_t DependencyObject::AddPropertyChangedCallback(
        std::shared_ptr<detail::PropertyInfo const> info, PropertyChangedHandler handler)
    {
        auto lifetime = get_strong();
        if (m_nextToken == std::numeric_limits<std::int64_t>::max())
        {
            throw winrt::hresult_illegal_method_call();
        }
        auto const token = m_nextToken + 1;
        m_subscriptions.push_back(std::make_shared<Subscription>(std::move(info), token, std::move(handler)));
        m_nextToken = token;
        return token;
    }

    void DependencyObject::RemovePropertyChangedCallback(detail::PropertyInfo const* info, std::int64_t token)
    {
        auto lifetime = get_strong();
        auto const found = std::ranges::find_if(m_subscriptions,
            [=](auto const& entry) { return entry->token == token && entry->property.get() == info; });
        if (found == m_subscriptions.end()) { return; }
        // Keep the removed subscription alive while erasing its registration.
        // Destroying a callback's captures can reenter registration.
        auto removed = std::move(*found);
        removed->active = false;
        m_subscriptions.erase(found);
    }

    void DependencyObject::NotifyPropertyChanged(
        std::shared_ptr<detail::PropertyInfo const> const& info, PropertyChange const& change)
    {
        auto lifetime = get_strong();
        // Snapshot the subscriptions before calling user code. Revoked handlers
        // are suppressed, and newly registered handlers see the next change.
        auto const subscriptions = m_subscriptions;
        if (info->onChanged) { info->onChanged(*this, change.oldValue, change.newValue); }
        OnPropertyChanged(change);
        for (auto const& subscription : subscriptions)
        {
            if (subscription->active && subscription->property.get() == change.property)
            {
                subscription->handler(*this, change);
            }
        }
        if (info->inherits)
        {
            OnInheritedPropertyChanged(DependencyPropertyHandle(info), change.oldValue, GetValue(DependencyPropertyHandle(info)));
        }
    }

    void DependencyObject::NotifyInheritedValueChanged(DependencyPropertyHandle const& property, std::any const& before)
    {
        VerifyProperty(property);
        if (HasValueOverride(property)) { return; }
        auto const after = GetValue(property);
        if (!property.m_info->valuesEqual(before, after))
        {
            NotifyPropertyChanged(property.m_info,
                { property.Id(), property.m_info->effects, before, after });
        }
    }
}
