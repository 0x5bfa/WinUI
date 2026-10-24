export module Xaml.Runtime:Binding;

import std;
import winrt_base;
import :Properties;
import :DependencyObject;

export namespace xaml::runtime
{
    enum class BindingMode { OneWay, TwoWay, OneTime };

    class PropertyBinding final : public std::enable_shared_from_this<PropertyBinding>
    {
    public:
        using Converter = std::function<std::any(std::any const&)>;
        static std::shared_ptr<PropertyBinding> Connect(DependencyObject& source,
            DependencyPropertyHandle sourceProperty, DependencyObject& target,
            DependencyPropertyHandle targetProperty, BindingMode mode = BindingMode::OneWay,
            Converter convert = {}, Converter convertBack = {}, bool templateValue = false);
        ~PropertyBinding();
        PropertyBinding(PropertyBinding const&) = delete;
        PropertyBinding& operator=(PropertyBinding const&) = delete;
        void UpdateTarget();
        void UpdateSource();
        void Disconnect();

    private:
        PropertyBinding(DependencyObject& source, DependencyPropertyHandle sourceProperty,
            DependencyObject& target, DependencyPropertyHandle targetProperty,
            BindingMode mode, Converter convert, Converter convertBack, bool templateValue);
        winrt::weak_ref<DependencyObject> m_source;
        winrt::weak_ref<DependencyObject> m_target;
        DependencyPropertyHandle m_sourceProperty;
        DependencyPropertyHandle m_targetProperty;
        BindingMode m_mode;
        Converter m_convert;
        Converter m_convertBack;
        std::int64_t m_sourceToken{};
        std::int64_t m_targetToken{};
        std::uint64_t m_expressionToken{};
        bool m_template{};
        bool m_active{ true };
        bool m_updating{};
        bool m_pending{};
    };
}
