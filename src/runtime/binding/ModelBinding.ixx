export module Xaml.Runtime:ModelBinding;

import std;
import winrt_base;
import :Properties;
import :DependencyObject;
import :Binding;

export namespace xaml::runtime
{
    // Adapter for observable native data sources. WinRT collection/property
    // adapters can implement this contract without coupling it to UI objects.
    class ObservableModel
    {
    public:
        using Handler = std::function<void(std::wstring_view)>;
        virtual ~ObservableModel() = default;
        [[nodiscard]] virtual std::any GetProperty(std::wstring_view name) const = 0;
        virtual void SetProperty(std::wstring_view name, std::any value) = 0;
        virtual std::uint64_t Subscribe(Handler callback) = 0;
        virtual void Unsubscribe(std::uint64_t token) = 0;
    };

    // General-purpose path binding: nested values must be stored as
    // std::shared_ptr<ObservableModel>, with each level observed separately.
    class ModelBinding final : public std::enable_shared_from_this<ModelBinding>
    {
    public:
        static std::shared_ptr<ModelBinding> Connect(std::shared_ptr<ObservableModel> source,
            std::wstring path, DependencyObject& target, DependencyPropertyHandle targetProperty,
            BindingMode mode = BindingMode::OneWay);
        ~ModelBinding();
        void UpdateTarget();
        void UpdateSource();
        void Disconnect();
    private:
        ModelBinding(std::shared_ptr<ObservableModel> source,
            std::vector<std::wstring> segments, DependencyObject& target,
            DependencyPropertyHandle targetProperty, BindingMode mode);
        [[nodiscard]] std::pair<std::shared_ptr<ObservableModel>, std::any> ObservePath();
        void UnsubscribeSources();
        struct Subscription
        {
            std::shared_ptr<ObservableModel> model;
            std::uint64_t token{};
        };
        std::shared_ptr<ObservableModel> m_source;
        std::vector<std::wstring> m_segments;
        winrt::weak_ref<DependencyObject> m_target;
        DependencyPropertyHandle m_property;
        BindingMode m_mode{};
        std::vector<Subscription> m_subscriptions;
        std::uint64_t m_expressionToken{};
        std::int64_t m_targetToken{};
        bool m_active{ true }, m_updating{}, m_pending{};
    };
}
