module Xaml.Runtime;

import std;
import winrt_base;

namespace xaml::runtime
{
    DependencyProperty<winrt::Windows::Foundation::IInspectable> const FrameworkElement::DataContextProperty =
        DependencyProperty<winrt::Windows::Foundation::IInspectable>::RegisterInherited<FrameworkElement>("DataContext", nullptr);
    DependencyProperty<ElementTheme> const FrameworkElement::RequestedThemeProperty =
        DependencyProperty<ElementTheme>::RegisterInherited<FrameworkElement>("RequestedTheme", ElementTheme::Default,
            PropertyEffects::Render, +[](ElementTheme theme)
            { return theme >= ElementTheme::Default && theme <= ElementTheme::HighContrast; });

    FrameworkElement::~FrameworkElement()
    {
        if (m_resourceSubscription && HasThreadAccess()) { ResourceDictionary::Unsubscribe(m_resourceSubscription); }
    }

    winrt::Windows::Foundation::IInspectable FrameworkElement::DataContext() const { return GetValue(DataContextProperty); }
    void FrameworkElement::DataContext(winrt::Windows::Foundation::IInspectable const& value) { SetValue(DataContextProperty, value); }
    ElementTheme FrameworkElement::RequestedTheme() const { return GetValue(RequestedThemeProperty); }
    void FrameworkElement::RequestedTheme(ElementTheme theme) { SetValue(RequestedThemeProperty, theme); }
    ElementTheme FrameworkElement::ActualTheme() const
    {
        auto const requested = RequestedTheme();
        return requested == ElementTheme::Default ? ElementTheme::Light : requested;
    }
    std::shared_ptr<ResourceDictionary> FrameworkElement::Resources()
    {
        VerifyAccess();
        if (!m_resources) { m_resources = std::make_shared<ResourceDictionary>(); }
        return m_resources;
    }
    std::optional<std::any> FrameworkElement::TryFindResource(std::wstring_view key) const
    {
        VerifyAccess();
        auto theme = ActualTheme();
        winrt::com_ptr<UIElement> current;
        current.copy_from(const_cast<FrameworkElement*>(this));
        while (current)
        {
            if (auto element = dynamic_cast<FrameworkElement*>(current.get()); element && element->m_resources)
            {
                if (auto value = element->m_resources->Lookup(key, theme)) { return value; }
            }
            current = current->Parent();
        }
        if (auto value = ResourceDictionary::Application()->Lookup(key, theme)) { return value; }
        return ResourceDictionary::Defaults()->Lookup(key, theme);
    }
    void FrameworkElement::WatchResources()
    {
        VerifyAccess();
        if (m_resourceSubscription) { return; }
        auto weak = DependencyObject::get_weak();
        m_resourceSubscription = ResourceDictionary::Subscribe([weak]
        {
            if (auto owner = weak.get())
            {
                auto element = dynamic_cast<FrameworkElement*>(owner.get());
                if (element) { element->RefreshResources(); }
            }
        });
    }
    void FrameworkElement::SetResourceReference(DependencyPropertyHandle property, std::wstring key, bool templateValue)
    {
        VerifyAccess();
        if (key.empty()) { throw winrt::hresult_invalid_argument(); }
        WatchResources();
        auto weak = DependencyObject::get_weak();
        auto identity = property.Id();
        auto token = AttachExpression(property, [weak, identity, templateValue]
        {
            if (auto owner = weak.get())
            {
                if (auto element = dynamic_cast<FrameworkElement*>(owner.get()))
                {
                    std::erase_if(element->m_resourceReferences, [=](auto const& item)
                        { return item.property.Id() == identity && item.templateValue == templateValue; });
                }
            }
        }, templateValue);
        m_resourceReferences.push_back({ std::move(property), std::move(key), token, templateValue });
        RefreshResources();
    }
    void FrameworkElement::RefreshResources()
    {
        VerifyAccess();
        auto lifetime = get_strong();
        auto references = m_resourceReferences;
        for (auto const& reference : references)
        {
            if (!IsExpressionCurrent(reference.property, reference.token, reference.templateValue)) { continue; }
            if (auto value = TryFindResource(reference.key))
            {
                SetExpressionValue(reference.property, reference.token, std::move(*value), reference.templateValue);
            }
            else { ClearExpressionValue(reference.property, reference.token, reference.templateValue); }
        }
        OnResourcesChanged();
        InvalidateRender();
    }
    void FrameworkElement::OnVisualAncestryChanged()
    {
        if (m_resourceSubscription) { RefreshResources(); }
    }
    void FrameworkElement::OnVisualParentChanged()
    {
        if (m_resourceSubscription) { RefreshResources(); }
    }
    void FrameworkElement::StyleValue(std::shared_ptr<Style> style)
    {
        VerifyAccess();
        auto lifetime = get_strong();
        auto setters = style ? style->Flatten(*this) : std::vector<Setter>{};
        if (style) { style->Seal(); }
        auto const version = ++m_styleVersion;
        auto previous = m_styleProperties;
        // Publish the union before setters invoke callbacks. A reentrant style
        // replacement must see every property that still has an old value.
        for (auto const& setter : setters)
        {
            if (std::ranges::none_of(m_styleProperties, [&](auto const& property) { return property.Id() == setter.property.Id(); }))
            { m_styleProperties.push_back(setter.property); }
        }
        m_style = std::move(style);
        OnStyleChanged();
        if (version != m_styleVersion) { return; }
        for (auto const& setter : setters)
        {
            SetStyleValue(setter.property, setter.value);
            if (version != m_styleVersion) { return; }
        }
        for (auto const& property : previous)
        {
            if (std::ranges::none_of(setters, [&](auto const& setter) { return setter.property.Id() == property.Id(); }))
            { ClearStyleValue(property); }
            if (version != m_styleVersion) { return; }
        }
        m_styleProperties.clear();
        for (auto const& setter : setters) { m_styleProperties.push_back(setter.property); }
    }
    std::shared_ptr<Style> FrameworkElement::StyleValue() const { VerifyAccess(); return m_style; }
    void FrameworkElement::SetNameScope(std::shared_ptr<NameScope> scope) { VerifyAccess(); m_nameScope = std::move(scope); }
    winrt::com_ptr<DependencyObject> FrameworkElement::FindName(std::wstring_view name) const
    {
        VerifyAccess();
        winrt::com_ptr<UIElement> current;
        current.copy_from(const_cast<FrameworkElement*>(this));
        while (current)
        {
            if (auto element = dynamic_cast<FrameworkElement*>(current.get()); element && element->m_nameScope)
            {
                return element->m_nameScope->Find(name);
            }
            current = current->Parent();
        }
        return nullptr;
    }
    void FrameworkElement::TemplatedParent(FrameworkElement* parent)
    {
        VerifyAccess();
        if (parent) { parent->VerifyAccess(); }
        m_templatedParent = parent ? static_cast<DependencyObject&>(*parent).get_weak() : winrt::weak_ref<DependencyObject>{};
    }
    winrt::com_ptr<FrameworkElement> FrameworkElement::TemplatedParent() const
    {
        VerifyAccess();
        auto owner = m_templatedParent.get();
        winrt::com_ptr<FrameworkElement> result;
        if (owner) { result.copy_from(dynamic_cast<FrameworkElement*>(owner.get())); }
        return result;
    }
}
