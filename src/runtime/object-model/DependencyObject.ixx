export module Xaml.Runtime:Properties;

import std;
import winrt_base;
import :ObjectModel;

export namespace xaml::runtime
{
    enum class PropertyEffects : unsigned
    {
        None = 0,
        Measure = 1,
        Arrange = 2,
        Render = 4
    };

    constexpr PropertyEffects operator|(PropertyEffects left, PropertyEffects right) noexcept
    {
        return static_cast<PropertyEffects>(static_cast<unsigned>(left) | static_cast<unsigned>(right));
    }

    constexpr bool HasEffect(PropertyEffects value, PropertyEffects effect) noexcept
    {
        return (static_cast<unsigned>(value) & static_cast<unsigned>(effect)) != 0;
    }

    struct DependencyObject;

    namespace detail
    {
        struct PropertyInfo
        {
            std::string name;
            PropertyEffects effects;
            bool (*acceptsOwner)(DependencyObject const&);
        };

        template <typename T>
        bool EqualValues(T left, T right) noexcept
        {
            if constexpr (std::is_floating_point_v<T>)
            {
                if (std::isnan(left) && std::isnan(right))
                {
                    return true;
                }
            }
            return left == right;
        }
    }

    // Reference-valued properties need tracker-aware storage. Small value
    // structs (such as Thickness) have no framework ownership edges.
    template <typename T>
    concept PropertyValue = std::is_arithmetic_v<T> || std::is_enum_v<T> ||
        (std::is_class_v<T> && std::is_trivially_copyable_v<T> && std::equality_comparable<T>);

    template <PropertyValue T>
    class DependencyProperty
    {
    public:
        template <typename Owner>
        static DependencyProperty Register(std::string_view name, T defaultValue,
            PropertyEffects effects = PropertyEffects::None, bool (*validate)(T) = nullptr)
        {
            static_assert(std::is_base_of_v<DependencyObject, Owner>);
            if (name.empty() || (validate && !validate(defaultValue)))
            {
                throw winrt::hresult_invalid_argument();
            }
            return DependencyProperty(std::make_shared<detail::PropertyInfo const>(
                std::string(name), effects,
                +[](DependencyObject const& owner) { return dynamic_cast<Owner const*>(&owner) != nullptr; }),
                defaultValue, validate);
        }

        [[nodiscard]] void const* Id() const noexcept { return m_info.get(); }

    private:
        friend struct DependencyObject;

        DependencyProperty(std::shared_ptr<detail::PropertyInfo const> info, T defaultValue, bool (*validate)(T))
            : m_info(std::move(info)), m_default(defaultValue), m_validate(validate) {}

        std::shared_ptr<detail::PropertyInfo const> m_info;
        T m_default;
        bool (*m_validate)(T);
    };

    struct PropertyChange
    {
        void const* property;
        PropertyEffects effects;
    };

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
