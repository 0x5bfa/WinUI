export module Xaml.Runtime:Styling;

import std;
import winrt_base;
import :Properties;
import :DependencyObject;

export namespace xaml::runtime
{
    enum class ElementTheme { Default, Light, Dark, HighContrast };

    class ResourceDictionary final
    {
    public:
        ResourceDictionary();
        void Insert(std::wstring key, std::any value);
        bool Remove(std::wstring_view key);
        void Clear();
        void AddMerged(std::shared_ptr<ResourceDictionary> dictionary);
        void SetTheme(ElementTheme theme, std::shared_ptr<ResourceDictionary> dictionary);
        [[nodiscard]] std::optional<std::any> Lookup(std::wstring_view key,
            ElementTheme theme = ElementTheme::Light) const;
        static std::shared_ptr<ResourceDictionary> Application();
        static std::shared_ptr<ResourceDictionary> Defaults();
        using Changed = std::function<void()>;
        static std::uint64_t Subscribe(Changed callback);
        static void Unsubscribe(std::uint64_t token);
        static void NotifyChanged();

    private:
        void VerifyAccess() const;
        [[nodiscard]] bool References(ResourceDictionary const* candidate,
            std::unordered_set<ResourceDictionary const*>& visited) const;
        [[nodiscard]] std::optional<std::any> LookupCore(std::wstring_view key, ElementTheme theme,
            std::unordered_set<ResourceDictionary const*>& visited) const;
        std::thread::id m_thread;
        std::map<std::wstring, std::any, std::less<>> m_values;
        std::vector<std::shared_ptr<ResourceDictionary>> m_merged;
        std::map<ElementTheme, std::shared_ptr<ResourceDictionary>> m_themes;
    };

    struct Setter
    {
        DependencyPropertyHandle property;
        std::any value;
        template <PropertyValue T>
        Setter(DependencyProperty<T> const& property, T value)
            : property(property.Erase()), value(std::move(value)) {}
        Setter(DependencyPropertyHandle property, std::any value)
            : property(std::move(property)), value(std::move(value)) {}
    };

    class Style final
    {
    public:
        using TargetCheck = std::function<bool(DependencyObject const&)>;
        explicit Style(TargetCheck accepts = {});
        template <typename T> static std::shared_ptr<Style> For()
        {
            return std::make_shared<Style>([](DependencyObject const& target)
                { return dynamic_cast<T const*>(&target) != nullptr; });
        }
        void BasedOn(std::shared_ptr<Style> style);
        void Add(Setter setter);
        void Seal();
        [[nodiscard]] std::vector<Setter> Flatten(DependencyObject const& target) const;
    private:
        TargetCheck m_accepts;
        std::shared_ptr<Style> m_base;
        std::vector<Setter> m_setters;
        bool m_sealed{};
    };

    class NameScope final
    {
    public:
        void Register(std::wstring name, DependencyObject& object);
        void Unregister(std::wstring_view name);
        [[nodiscard]] winrt::com_ptr<DependencyObject> Find(std::wstring_view name) const;
    private:
        std::thread::id m_thread{ std::this_thread::get_id() };
        std::map<std::wstring, winrt::weak_ref<DependencyObject>, std::less<>> m_names;
    };
}
