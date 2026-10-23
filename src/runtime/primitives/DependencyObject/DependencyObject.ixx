export module Xaml.Runtime:DependencyObject;

import std;
import winrt_base;
import :ObjectModel;
import :Properties;

export namespace xaml::runtime
{
    struct DependencyObject : winrt::implements<DependencyObject, Object>
    {
        template <PropertyValue T>
        [[nodiscard]] T GetValue(DependencyProperty<T> const& property) const
        {
            VerifyProperty(property);
            auto const found = m_values.find(property.m_info.get());
            return found == m_values.end() ? property.m_default : std::any_cast<T>(found->second.value);
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
            VerifyProperty(property);
            auto lifetime = get_strong();
            auto const descriptor = property;
            if (descriptor.m_validate && !descriptor.m_validate(value))
            {
                throw winrt::hresult_invalid_argument();
            }
            // Validation can reenter and write this property. Compare against
            // the committed value after it returns, while retaining the owner.
            auto const oldValue = GetValue(descriptor);
            auto const info = descriptor.m_info;
            m_values.insert_or_assign(info.get(), Entry{ info, std::any(value) });
            if (!detail::EqualValues(oldValue, value))
            {
                // Storage is committed before a reentrant notification. A throwing
                // notification does not roll back application-visible state.
                OnPropertyChanged({ info.get(), info->effects });
            }
        }

        template <PropertyValue T>
        void ClearValue(DependencyProperty<T> const& property)
        {
            auto const oldValue = GetValue(property);
            auto lifetime = get_strong();
            auto const info = property.m_info;
            if (m_values.erase(info.get()) != 0 && !detail::EqualValues(oldValue, property.m_default))
            {
                OnPropertyChanged({ info.get(), info->effects });
            }
        }

    protected:
        virtual void OnPropertyChanged(PropertyChange const&) {}

    private:
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
    };
}
