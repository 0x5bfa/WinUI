export module Xaml.Runtime:Properties;

import std;
import winrt_base;
import winrt.Windows.Foundation;

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

    struct BoxedPropertyMetadata
    {
        winrt::Windows::Foundation::IInspectable defaultValue{ nullptr };
        std::function<void(DependencyObject&, winrt::Windows::Foundation::IInspectable const&,
            winrt::Windows::Foundation::IInspectable const&)> onChanged;
        std::function<winrt::Windows::Foundation::IInspectable(DependencyObject&)> createDefault;
    };

    namespace detail
    {
        struct PropertyInfo
        {
            std::string name;
            PropertyEffects effects;
            bool (*acceptsOwner)(DependencyObject const&);
            std::function<void(DependencyObject&, std::any const&, std::any const&)> onChanged;
            std::function<std::any(DependencyObject&)> createDefault;
            std::type_index valueType{ typeid(void) };
            std::any defaultValue;
            std::function<bool(std::any const&)> acceptsValue;
            std::function<bool(std::any const&, std::any const&)> valuesEqual;
            std::function<winrt::Windows::Foundation::IInspectable(std::any const&)> box;
            std::function<std::any(winrt::Windows::Foundation::IInspectable const&)> unbox;
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

    // WinRT object values retain COM references. Managed cycles require
    // IReferenceTracker integration before public XAML ABI exposure.
    template <typename T>
    concept PropertyValue = std::is_arithmetic_v<T> || std::is_enum_v<T> ||
        (std::is_class_v<T> && std::is_trivially_copyable_v<T> && std::equality_comparable<T>) ||
        std::same_as<T, winrt::Windows::Foundation::IInspectable> ||
        std::same_as<T, winrt::hstring>;

    // Native metadata used by the property engine. Public WinRT PropertyMetadata
    // will be projected from the contract and backed by this same state.
    template <PropertyValue T>
    struct PropertyMetadata final
    {
        using ChangedHandler = std::function<void(DependencyObject&, T, T)>;
        using DefaultFactory = std::function<T(DependencyObject&)>;

        T defaultValue{};
        DefaultFactory createDefault;
        ChangedHandler onChanged;

        explicit PropertyMetadata(T value, ChangedHandler changed = {})
            : defaultValue(std::move(value)), onChanged(std::move(changed)) {}

        explicit PropertyMetadata(DefaultFactory factory, ChangedHandler changed = {})
            : createDefault(std::move(factory)), onChanged(std::move(changed))
        {
            if (!createDefault) { throw winrt::hresult_invalid_argument(); }
        }
    };

    // A type-erased property identity for the future non-generic WinRT ABI.
    // It shares registration metadata and value storage with typed properties.
    class DependencyPropertyHandle final
    {
    public:
        [[nodiscard]] static winrt::Windows::Foundation::IInspectable UnsetValue();
        [[nodiscard]] std::string_view Name() const noexcept { return m_info->name; }
        [[nodiscard]] std::type_index ValueType() const noexcept { return m_info->valueType; }
        [[nodiscard]] void const* Id() const noexcept { return m_info.get(); }
        [[nodiscard]] BoxedPropertyMetadata GetMetadata() const
        {
            BoxedPropertyMetadata metadata;
            if (m_info->onChanged)
            {
                metadata.onChanged = [handler = m_info->onChanged, unbox = m_info->unbox](
                    DependencyObject& owner, winrt::Windows::Foundation::IInspectable const& before,
                    winrt::Windows::Foundation::IInspectable const& after)
                {
                    handler(owner, unbox(before), unbox(after));
                };
            }
            if (m_info->createDefault)
            {
                metadata.createDefault = [factory = m_info->createDefault, box = m_info->box](DependencyObject& owner)
                {
                    return box(factory(owner));
                };
            }
            else
            {
                metadata.defaultValue = m_info->box(m_info->defaultValue);
            }
            return metadata;
        }

    private:
        template <PropertyValue T> friend class DependencyProperty;
        friend struct DependencyObject;
        explicit DependencyPropertyHandle(std::shared_ptr<detail::PropertyInfo const> info)
            : m_info(std::move(info)) {}
        std::shared_ptr<detail::PropertyInfo const> m_info;
    };

    template <PropertyValue T>
    class DependencyProperty
    {
    public:
        using ChangedHandler = std::function<void(DependencyObject&, T, T)>;

        template <typename Owner>
        static DependencyProperty Register(std::string_view name, T defaultValue,
            PropertyEffects effects = PropertyEffects::None, bool (*validate)(T) = nullptr,
            ChangedHandler onChanged = {})
        {
            return RegisterWithMetadata<Owner>(name, PropertyMetadata<T>(defaultValue, std::move(onChanged)),
                effects, validate);
        }

        template <typename Owner>
        static DependencyProperty RegisterAttached(std::string_view name, T defaultValue,
            PropertyEffects effects = PropertyEffects::None, bool (*validate)(T) = nullptr,
            ChangedHandler onChanged = {})
        {
            return RegisterAttachedWithMetadata<Owner>(name,
                PropertyMetadata<T>(defaultValue, std::move(onChanged)), effects, validate);
        }

        template <typename Owner>
        static DependencyProperty RegisterWithMetadata(std::string_view name, PropertyMetadata<T> metadata,
            PropertyEffects effects = PropertyEffects::None, bool (*validate)(T) = nullptr)
        {
            static_assert(std::is_base_of_v<DependencyObject, Owner>);
            return RegisterCore(name, std::move(metadata), effects, validate,
                +[](DependencyObject const& owner) { return dynamic_cast<Owner const*>(&owner) != nullptr; });
        }

        template <typename Owner>
        static DependencyProperty RegisterAttachedWithMetadata(std::string_view name, PropertyMetadata<T> metadata,
            PropertyEffects effects = PropertyEffects::None, bool (*validate)(T) = nullptr)
        {
            static_assert(std::is_base_of_v<DependencyObject, Owner>);
            return RegisterCore(name, std::move(metadata), effects, validate,
                +[](DependencyObject const&) { return true; });
        }

        [[nodiscard]] void const* Id() const noexcept { return m_info.get(); }
        [[nodiscard]] DependencyPropertyHandle Erase() const { return DependencyPropertyHandle(m_info); }
        [[nodiscard]] std::string_view Name() const noexcept { return m_info->name; }
        [[nodiscard]] T DefaultValue() const
        {
            if (m_info->createDefault) { throw winrt::hresult_illegal_method_call(); }
            return m_default;
        }

    private:
        friend struct DependencyObject;

        static DependencyProperty RegisterCore(std::string_view name, PropertyMetadata<T> metadata,
            PropertyEffects effects, bool (*validate)(T), bool (*ownerCheck)(DependencyObject const&))
        {
            if (name.empty() || (!metadata.createDefault && validate && !validate(metadata.defaultValue)))
            {
                throw winrt::hresult_invalid_argument();
            }
            auto factory = std::function<std::any(DependencyObject&)>{};
            if (metadata.createDefault)
            {
                factory = [create = std::move(metadata.createDefault), validate](DependencyObject& owner)
                {
                    auto value = create(owner);
                    if (validate && !validate(value)) { throw winrt::hresult_invalid_argument(); }
                    return std::any(std::move(value));
                };
            }
            // Preserve the declared default in both the erased registry and
            // typed descriptor before moving either value into its owner.
            T const registeredDefault = metadata.defaultValue;
            auto info = std::make_shared<detail::PropertyInfo const>(
                std::string(name), effects, ownerCheck,
                WrapChangedHandler(std::move(metadata.onChanged)), std::move(factory),
                std::type_index(typeid(T)), std::any(registeredDefault),
                [validate](std::any const& candidate)
                {
                    return candidate.type() == typeid(T) &&
                        (!validate || validate(std::any_cast<T const&>(candidate)));
                },
                [](std::any const& left, std::any const& right)
                {
                    return detail::EqualValues(std::any_cast<T const&>(left), std::any_cast<T const&>(right));
                },
                [](std::any const& value) -> winrt::Windows::Foundation::IInspectable
                {
                    if constexpr (std::same_as<T, winrt::Windows::Foundation::IInspectable>)
                    {
                        return std::any_cast<T const&>(value);
                    }
                    else if constexpr (std::same_as<T, bool> || std::same_as<T, std::int32_t> ||
                        std::same_as<T, std::int64_t> || std::same_as<T, std::uint32_t> ||
                        std::same_as<T, std::uint64_t> || std::same_as<T, float> ||
                        std::same_as<T, double> || std::same_as<T, winrt::hstring>)
                    {
                        return winrt::box_value(std::any_cast<T const&>(value));
                    }
                    else if constexpr (std::is_enum_v<T> && sizeof(T) <= sizeof(std::int32_t))
                    {
                        return winrt::box_value(static_cast<std::int32_t>(std::any_cast<T const&>(value)));
                    }
                    else { throw winrt::hresult_not_implemented(); }
                },
                [](winrt::Windows::Foundation::IInspectable const& value) -> std::any
                {
                    if constexpr (std::same_as<T, winrt::Windows::Foundation::IInspectable>)
                    {
                        return std::any(value);
                    }
                    else if constexpr (std::same_as<T, bool> || std::same_as<T, std::int32_t> ||
                        std::same_as<T, std::int64_t> || std::same_as<T, std::uint32_t> ||
                        std::same_as<T, std::uint64_t> || std::same_as<T, float> ||
                        std::same_as<T, double> || std::same_as<T, winrt::hstring>)
                    {
                        // An incompatible boxed value is a property type error.
                        // QueryInterface otherwise leaks E_NOINTERFACE to SetValue.
                        try { return std::any(winrt::unbox_value<T>(value)); }
                        catch (winrt::hresult_no_interface const&)
                        {
                            throw winrt::hresult_invalid_argument();
                        }
                    }
                    else if constexpr (std::is_enum_v<T> && sizeof(T) <= sizeof(std::int32_t))
                    {
                        try { return std::any(static_cast<T>(winrt::unbox_value<std::int32_t>(value))); }
                        catch (winrt::hresult_no_interface const&)
                        {
                            throw winrt::hresult_invalid_argument();
                        }
                    }
                    else { throw winrt::hresult_not_implemented(); }
                });
            return DependencyProperty(std::move(info), registeredDefault, validate);
        }

        static std::function<void(DependencyObject&, std::any const&, std::any const&)>
            WrapChangedHandler(ChangedHandler handler)
        {
            if (!handler) { return {}; }
            return [handler = std::move(handler)](DependencyObject& owner,
                std::any const& previous, std::any const& current)
            {
                handler(owner, std::any_cast<T>(previous), std::any_cast<T>(current));
            };
        }

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
        std::any oldValue;
        std::any newValue;
    };

    // Native non-generic property registration for a future WinRT static factory.
    // The WinRT TypeName and PropertyMetadata projection is not exposed yet.
    enum class BoxedPropertyKind { Boolean, Int32, UInt32, Int64, UInt64, Single, Double, String, Object };

    namespace detail
    {
        template <typename T>
        T UnboxMetadataValue(winrt::Windows::Foundation::IInspectable const& value)
        {
            if constexpr (std::same_as<T, winrt::Windows::Foundation::IInspectable>)
            {
                return value;
            }
            else if constexpr (std::same_as<T, winrt::hstring>)
            {
                if (!value) { return {}; }
                try { return winrt::unbox_value<T>(value); }
                catch (winrt::hresult_no_interface const&) { throw winrt::hresult_invalid_argument(); }
            }
            else
            {
                if (!value) { throw winrt::hresult_invalid_argument(); }
                try { return winrt::unbox_value<T>(value); }
                catch (winrt::hresult_no_interface const&) { throw winrt::hresult_invalid_argument(); }
            }
        }

        template <typename T, typename Owner>
        DependencyPropertyHandle RegisterBoxedProperty(std::string_view name,
            BoxedPropertyMetadata const& metadata, PropertyEffects effects, bool attached)
        {
            auto changed = [callback = metadata.onChanged](DependencyObject& object, T before, T after)
            {
                if (!callback) { return; }
                if constexpr (std::same_as<T, winrt::Windows::Foundation::IInspectable>)
                {
                    callback(object, before, after);
                }
                else
                {
                    callback(object, winrt::box_value(before), winrt::box_value(after));
                }
            };
            auto nativeMetadata = metadata.createDefault
                ? PropertyMetadata<T>(typename PropertyMetadata<T>::DefaultFactory(
                    [factory = metadata.createDefault](DependencyObject& owner)
                    {
                        return UnboxMetadataValue<T>(factory(owner));
                    }), std::move(changed))
                : PropertyMetadata<T>(UnboxMetadataValue<T>(metadata.defaultValue), std::move(changed));
            if (attached)
            {
                return DependencyProperty<T>::template RegisterAttachedWithMetadata<Owner>(
                    name, std::move(nativeMetadata), effects).Erase();
            }
            return DependencyProperty<T>::template RegisterWithMetadata<Owner>(
                name, std::move(nativeMetadata), effects).Erase();
        }
    }

    template <typename Owner>
    DependencyPropertyHandle RegisterBoxedProperty(std::string_view name, BoxedPropertyKind kind,
        BoxedPropertyMetadata const& metadata, PropertyEffects effects = PropertyEffects::None,
        bool attached = false)
    {
        switch (kind)
        {
        case BoxedPropertyKind::Boolean:
            return detail::RegisterBoxedProperty<bool, Owner>(name, metadata, effects, attached);
        case BoxedPropertyKind::Int32:
            return detail::RegisterBoxedProperty<std::int32_t, Owner>(name, metadata, effects, attached);
        case BoxedPropertyKind::UInt32:
            return detail::RegisterBoxedProperty<std::uint32_t, Owner>(name, metadata, effects, attached);
        case BoxedPropertyKind::Int64:
            return detail::RegisterBoxedProperty<std::int64_t, Owner>(name, metadata, effects, attached);
        case BoxedPropertyKind::UInt64:
            return detail::RegisterBoxedProperty<std::uint64_t, Owner>(name, metadata, effects, attached);
        case BoxedPropertyKind::Single:
            return detail::RegisterBoxedProperty<float, Owner>(name, metadata, effects, attached);
        case BoxedPropertyKind::Double:
            return detail::RegisterBoxedProperty<double, Owner>(name, metadata, effects, attached);
        case BoxedPropertyKind::String:
            return detail::RegisterBoxedProperty<winrt::hstring, Owner>(name, metadata, effects, attached);
        case BoxedPropertyKind::Object:
            return detail::RegisterBoxedProperty<winrt::Windows::Foundation::IInspectable, Owner>(
                name, metadata, effects, attached);
        }
        throw winrt::hresult_invalid_argument();
    }

}
