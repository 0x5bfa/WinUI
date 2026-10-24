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

    private:
        std::thread::id const m_thread;
        void VerifyProperty(DependencyPropertyHandle const& property) const;
        [[nodiscard]] std::any GetDefaultValue(DependencyPropertyHandle const& property) const;
        [[nodiscard]] std::any EvaluateValue(DependencyPropertyHandle const& property,
            bool includeAnimation) const;
        enum class ValueLayer { Local, Style, Animation };
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
        std::unordered_map<detail::PropertyInfo const*, Entry> m_animationValues;
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
    };
}
