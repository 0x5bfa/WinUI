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

}
