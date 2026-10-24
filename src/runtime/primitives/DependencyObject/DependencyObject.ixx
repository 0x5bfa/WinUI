export module Xaml.Runtime:DependencyObject;

import std;
import winrt_base;
import winrt.Microsoft.UI.Dispatching;
import :Properties;

export namespace xaml::runtime
{
    struct DependencyObject : winrt::implements<DependencyObject, winrt::Windows::Foundation::IInspectable>
    {
        using PropertyChangedHandler = std::function<void(DependencyObject&, PropertyChange const&)>;

        DependencyObject() noexcept;

        [[nodiscard]] bool HasThreadAccess() const noexcept;
        [[nodiscard]] winrt::Microsoft::UI::Dispatching::DispatcherQueue DispatcherQueue() const;
        void VerifyAccess() const;
        void abi_enter() const;

        [[nodiscard]] std::any GetValue(DependencyPropertyHandle const& property) const;
        [[nodiscard]] std::any GetAnimationBaseValue(DependencyPropertyHandle const& property) const;
        [[nodiscard]] winrt::Windows::Foundation::IInspectable GetBoxedValue(
            DependencyPropertyHandle const& property) const;
        [[nodiscard]] winrt::Windows::Foundation::IInspectable ReadLocalBoxedValue(
            DependencyPropertyHandle const& property) const;
        [[nodiscard]] winrt::Windows::Foundation::IInspectable GetAnimationBaseBoxedValue(
            DependencyPropertyHandle const& property) const;
        [[nodiscard]] std::optional<std::any> ReadLocalValue(DependencyPropertyHandle const& property) const;
        [[nodiscard]] bool HasValueOverride(DependencyPropertyHandle const& property) const;
        void SetValue(DependencyPropertyHandle const& property, std::any value);
        void SetStyleValue(DependencyPropertyHandle const& property, std::any value);
        void ClearStyleValue(DependencyPropertyHandle const& property);
        void SetAnimationValue(DependencyPropertyHandle const& property, std::any value);
        void ClearAnimationValue(DependencyPropertyHandle const& property);
        void SetTemplateValue(DependencyPropertyHandle const& property, std::any value);
        void ClearTemplateValue(DependencyPropertyHandle const& property);
        void SetCurrentValue(DependencyPropertyHandle const& property, std::any value);
        void SetStateValue(DependencyPropertyHandle const& property, std::uint64_t contributor, std::any value);
        void ClearStateValue(DependencyPropertyHandle const& property, std::uint64_t contributor);
        void ValidateValue(DependencyPropertyHandle const& property, std::any const& value) const;
        [[nodiscard]] std::uint64_t AttachExpression(DependencyPropertyHandle const& property,
            std::function<void()> disconnect, bool templateValue = false);
        [[nodiscard]] bool IsExpressionCurrent(DependencyPropertyHandle const& property,
            std::uint64_t token, bool templateValue = false) const;
        void SetExpressionValue(DependencyPropertyHandle const& property, std::uint64_t token,
            std::any value, bool templateValue = false);
        void DetachExpression(DependencyPropertyHandle const& property, std::uint64_t token,
            bool templateValue = false);
        void ClearExpressionValue(DependencyPropertyHandle const& property, std::uint64_t token,
            bool templateValue = false);
        void SetBoxedValue(DependencyPropertyHandle const& property,
            winrt::Windows::Foundation::IInspectable const& value);
        void ClearValue(DependencyPropertyHandle const& property);
        [[nodiscard]] std::int64_t RegisterPropertyChangedCallback(
            DependencyPropertyHandle const& property, PropertyChangedHandler handler);
        void UnregisterPropertyChangedCallback(DependencyPropertyHandle const& property, std::int64_t token);

        template <PropertyValue T>
        [[nodiscard]] std::optional<T> ReadLocalValue(DependencyProperty<T> const& property) const
        {
            VerifyProperty(property);
            auto const found = m_values.find(property.m_info.get());
            if (found == m_values.end()) { return std::nullopt; }
            return std::any_cast<T>(found->second.value);
        }

        template <PropertyValue T>
        [[nodiscard]] T GetAnimationBaseValue(DependencyProperty<T> const& property) const
        {
            return std::any_cast<T>(GetAnimationBaseValue(property.Erase()));
        }

        template <PropertyValue T>
        void SetStyleValue(DependencyProperty<T> const& property, T value)
        {
            SetStyleValue(property.Erase(), std::any(std::move(value)));
        }

        template <PropertyValue T>
        void ClearStyleValue(DependencyProperty<T> const& property)
        {
            ClearStyleValue(property.Erase());
        }

        template <PropertyValue T>
        void SetAnimationValue(DependencyProperty<T> const& property, T value)
        {
            SetAnimationValue(property.Erase(), std::any(std::move(value)));
        }

        template <PropertyValue T>
        void ClearAnimationValue(DependencyProperty<T> const& property)
        {
            ClearAnimationValue(property.Erase());
        }

        template <PropertyValue T>
        [[nodiscard]] std::int64_t RegisterPropertyChangedCallback(
            DependencyProperty<T> const& property, PropertyChangedHandler handler)
        {
            VerifyProperty(property);
            if (!handler) { throw winrt::hresult_invalid_argument(); }
            return AddPropertyChangedCallback(property.m_info, std::move(handler));
        }

        template <PropertyValue T>
        void UnregisterPropertyChangedCallback(DependencyProperty<T> const& property, std::int64_t token)
        {
            VerifyProperty(property);
            RemovePropertyChangedCallback(property.m_info.get(), token);
        }

        template <PropertyValue T>
        [[nodiscard]] T GetValue(DependencyProperty<T> const& property) const
        {
            return std::any_cast<T>(GetValue(property.Erase()));
        }

        template <PropertyValue T>
        [[nodiscard]] bool HasLocalValue(DependencyProperty<T> const& property) const
        {
            VerifyProperty(property);
            return m_values.contains(property.m_info.get());
        }

        template <PropertyValue T>
        void SetValue(DependencyProperty<T> const& property, T value)
        {
            SetValue(property.Erase(), std::any(std::move(value)));
        }

        template <PropertyValue T>
        void ClearValue(DependencyProperty<T> const& property)
        {
            ClearValue(property.Erase());
        }

    protected:
        virtual void OnPropertyChanged(PropertyChange const&) {}
        virtual std::optional<std::any> GetInheritedValue(DependencyPropertyHandle const&) const { return {}; }
        virtual void OnInheritedPropertyChanged(DependencyPropertyHandle const&, std::any const&, std::any const&) {}
        void NotifyInheritedValueChanged(DependencyPropertyHandle const& property, std::any const& before);

    private:
        std::thread::id const m_thread;
        void VerifyProperty(DependencyPropertyHandle const& property) const;
        [[nodiscard]] std::any GetDefaultValue(DependencyPropertyHandle const& property) const;
        [[nodiscard]] std::any EvaluateValue(DependencyPropertyHandle const& property,
            bool includeAnimation) const;
        enum class ValueLayer { Local, Template, Style, Animation };
        void DropExpression(DependencyPropertyHandle const& property, bool templateValue);
        void SetLayerValue(DependencyPropertyHandle const& property, std::any value, ValueLayer layer);
        void ClearLayerValue(DependencyPropertyHandle const& property, ValueLayer layer);

        template <PropertyValue T>
        [[nodiscard]] T GetDefaultValue(DependencyProperty<T> const& property) const
        {
            auto const* key = property.m_info.get();
            if (!property.m_info->createDefault) { return property.m_default; }
            if (auto found = m_createdDefaults.find(key); found != m_createdDefaults.end())
            {
                return std::any_cast<T>(found->second.value);
            }
            if (!m_creatingDefaults.insert(key).second)
            {
                throw winrt::hresult_illegal_method_call();
            }
            try
            {
                auto lifetime = const_cast<DependencyObject*>(this)->get_strong();
                auto value = property.m_info->createDefault(*const_cast<DependencyObject*>(this));
                m_createdDefaults.emplace(key, Entry{ property.m_info, std::move(value) });
                m_creatingDefaults.erase(key);
            }
            catch (...)
            {
                m_creatingDefaults.erase(key);
                throw;
            }
            return std::any_cast<T>(m_createdDefaults.at(key).value);
        }

        [[nodiscard]] std::int64_t AddPropertyChangedCallback(
            std::shared_ptr<detail::PropertyInfo const> info, PropertyChangedHandler handler);
        void RemovePropertyChangedCallback(detail::PropertyInfo const* info, std::int64_t token);
        void NotifyPropertyChanged(std::shared_ptr<detail::PropertyInfo const> const& info, PropertyChange const& change);

        template <PropertyValue T>
        void VerifyProperty(DependencyProperty<T> const& property) const
        {
            VerifyAccess();
            if (!property.m_info || !property.m_info->acceptsOwner(*this))
            {
                throw winrt::hresult_invalid_argument();
            }
        }

        struct Entry
        {
            std::shared_ptr<detail::PropertyInfo const> property;
            std::any value;
        };

        std::unordered_map<detail::PropertyInfo const*, Entry> m_values;
        std::unordered_map<detail::PropertyInfo const*, Entry> m_styleValues;
        std::unordered_map<detail::PropertyInfo const*, Entry> m_templateValues;
        std::unordered_map<detail::PropertyInfo const*, Entry> m_animationValues;
        std::unordered_map<detail::PropertyInfo const*, std::map<std::uint64_t, Entry>> m_stateValues;
        // Retain the descriptor for every cached factory default. Dynamic
        // registrations can otherwise free their metadata and reuse this key.
        mutable std::unordered_map<detail::PropertyInfo const*, Entry> m_createdDefaults;
        mutable std::unordered_set<detail::PropertyInfo const*> m_creatingDefaults;

        struct Subscription
        {
            std::shared_ptr<detail::PropertyInfo const> property;
            std::int64_t token;
            PropertyChangedHandler handler;
            bool active{ true };
        };

        std::vector<std::shared_ptr<Subscription>> m_subscriptions;
        std::int64_t m_nextToken{};
        struct Expression
        {
            std::shared_ptr<detail::PropertyInfo const> property;
            std::uint64_t token;
            std::function<void()> disconnect;
        };
        std::unordered_map<detail::PropertyInfo const*, Expression> m_localExpressions;
        std::unordered_map<detail::PropertyInfo const*, Expression> m_templateExpressions;
        std::uint64_t m_nextExpression{};
    };
}
