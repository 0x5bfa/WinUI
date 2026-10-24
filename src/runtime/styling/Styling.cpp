module Xaml.Runtime;

import std;
import winrt_base;

namespace xaml::runtime
{
    std::shared_ptr<ResourceDictionary> ResourceDictionary::Defaults()
    {
        thread_local auto resources = std::make_shared<ResourceDictionary>();
        return resources;
    }
    namespace
    {
        struct ResourceSubscription
        {
            std::uint64_t token;
            ResourceDictionary::Changed callback;
            bool active{ true };
        };
        thread_local std::vector<std::shared_ptr<ResourceSubscription>> resourceSubscriptions;
        thread_local std::uint64_t nextResourceToken{};
        thread_local bool notifyingResources{};
        thread_local bool resourcesPending{};
    }

    ResourceDictionary::ResourceDictionary() : m_thread(std::this_thread::get_id()) {}
    void ResourceDictionary::VerifyAccess() const
    {
        if (m_thread != std::this_thread::get_id()) { throw winrt::hresult_wrong_thread(); }
    }
    void ResourceDictionary::Insert(std::wstring key, std::any value)
    {
        VerifyAccess();
        if (key.empty() || !value.has_value()) { throw winrt::hresult_invalid_argument(); }
        m_values.insert_or_assign(std::move(key), std::move(value));
        NotifyChanged();
    }
    bool ResourceDictionary::Remove(std::wstring_view key)
    {
        VerifyAccess();
        auto it = m_values.find(key);
        if (it == m_values.end()) { return false; }
        m_values.erase(it);
        NotifyChanged();
        return true;
    }
    void ResourceDictionary::Clear()
    {
        VerifyAccess();
        m_values.clear(); m_merged.clear(); m_themes.clear(); NotifyChanged();
    }
    bool ResourceDictionary::References(ResourceDictionary const* candidate,
        std::unordered_set<ResourceDictionary const*>& visited) const
    {
        if (this == candidate) { return true; }
        if (!visited.insert(this).second) { return false; }
        for (auto const& dictionary : m_merged) { if (dictionary->References(candidate, visited)) { return true; } }
        for (auto const& [_, dictionary] : m_themes) { if (dictionary->References(candidate, visited)) { return true; } }
        return false;
    }
    void ResourceDictionary::AddMerged(std::shared_ptr<ResourceDictionary> dictionary)
    {
        VerifyAccess();
        if (!dictionary) { throw winrt::hresult_invalid_argument(); }
        dictionary->VerifyAccess();
        std::unordered_set<ResourceDictionary const*> visited;
        if (dictionary->References(this, visited)) { throw winrt::hresult_invalid_argument(); }
        m_merged.push_back(std::move(dictionary)); NotifyChanged();
    }
    void ResourceDictionary::SetTheme(ElementTheme theme, std::shared_ptr<ResourceDictionary> dictionary)
    {
        VerifyAccess();
        if (!dictionary) { m_themes.erase(theme); NotifyChanged(); return; }
        dictionary->VerifyAccess();
        std::unordered_set<ResourceDictionary const*> visited;
        if (dictionary->References(this, visited)) { throw winrt::hresult_invalid_argument(); }
        m_themes.insert_or_assign(theme, std::move(dictionary)); NotifyChanged();
    }
    std::optional<std::any> ResourceDictionary::LookupCore(std::wstring_view key, ElementTheme theme,
        std::unordered_set<ResourceDictionary const*>& visited) const
    {
        if (!visited.insert(this).second) { return {}; }
        if (auto found = m_values.find(key); found != m_values.end()) { return found->second; }
        auto themed = m_themes.find(theme);
        if (themed == m_themes.end()) { themed = m_themes.find(ElementTheme::Default); }
        if (themed != m_themes.end())
        {
            if (auto value = themed->second->LookupCore(key, theme, visited)) { return value; }
        }
        for (auto it = m_merged.rbegin(); it != m_merged.rend(); ++it)
        {
            if (auto value = (*it)->LookupCore(key, theme, visited)) { return value; }
        }
        return {};
    }
    std::optional<std::any> ResourceDictionary::Lookup(std::wstring_view key, ElementTheme theme) const
    {
        VerifyAccess();
        std::unordered_set<ResourceDictionary const*> visited;
        return LookupCore(key, theme, visited);
    }
    std::shared_ptr<ResourceDictionary> ResourceDictionary::Application()
    {
        thread_local auto resources = std::make_shared<ResourceDictionary>();
        return resources;
    }
    std::uint64_t ResourceDictionary::Subscribe(Changed callback)
    {
        if (!callback || nextResourceToken == std::numeric_limits<std::uint64_t>::max())
        {
            throw winrt::hresult_invalid_argument();
        }
        auto token = ++nextResourceToken;
        resourceSubscriptions.push_back(std::make_shared<ResourceSubscription>(token, std::move(callback)));
        return token;
    }
    void ResourceDictionary::Unsubscribe(std::uint64_t token)
    {
        for (auto const& entry : resourceSubscriptions) { if (entry->token == token) { entry->active = false; } }
        std::erase_if(resourceSubscriptions, [](auto const& entry) { return !entry->active; });
    }
    void ResourceDictionary::NotifyChanged()
    {
        resourcesPending = true;
        if (notifyingResources) { return; }
        notifyingResources = true;
        try
        {
            for (int pass = 0; pass < 32 && resourcesPending; ++pass)
            {
                resourcesPending = false;
                auto callbacks = resourceSubscriptions;
                for (auto const& entry : callbacks) { if (entry->active) { entry->callback(); } }
            }
            notifyingResources = false;
            if (resourcesPending) { throw winrt::hresult_illegal_method_call(); }
        }
        catch (...) { notifyingResources = false; resourcesPending = false; throw; }
    }

    Style::Style(TargetCheck accepts) : m_accepts(std::move(accepts)) {}
    void Style::BasedOn(std::shared_ptr<Style> style)
    {
        if (m_sealed) { throw winrt::hresult_illegal_method_call(); }
        for (auto current = style; current; current = current->m_base)
        {
            if (current.get() == this) { throw winrt::hresult_invalid_argument(); }
        }
        m_base = std::move(style);
    }
    void Style::Add(Setter setter)
    {
        if (m_sealed) { throw winrt::hresult_illegal_method_call(); }
        m_setters.push_back(std::move(setter));
    }
    void Style::Seal()
    {
        if (m_sealed) { return; }
        if (m_base) { m_base->Seal(); }
        m_sealed = true;
    }
    std::vector<Setter> Style::Flatten(DependencyObject const& target) const
    {
        target.VerifyAccess();
        if (m_accepts && !m_accepts(target)) { throw winrt::hresult_invalid_argument(); }
        auto result = m_base ? m_base->Flatten(target) : std::vector<Setter>{};
        for (auto const& setter : m_setters)
        {
            target.ValidateValue(setter.property, setter.value);
            auto found = std::ranges::find_if(result, [&](auto const& existing)
                { return existing.property.Id() == setter.property.Id(); });
            if (found == result.end()) { result.push_back(setter); }
            else { *found = setter; }
        }
        return result;
    }

    void NameScope::Register(std::wstring name, DependencyObject& object)
    {
        if (m_thread != std::this_thread::get_id()) { throw winrt::hresult_wrong_thread(); }
        object.VerifyAccess();
        if (name.empty() || (m_names.contains(name) && m_names.at(name).get()))
        {
            throw winrt::hresult_invalid_argument();
        }
        m_names.insert_or_assign(std::move(name), object.get_weak());
    }
    void NameScope::Unregister(std::wstring_view name)
    {
        if (m_thread != std::this_thread::get_id()) { throw winrt::hresult_wrong_thread(); }
        auto it = m_names.find(name);
        if (it != m_names.end()) { m_names.erase(it); }
    }
    winrt::com_ptr<DependencyObject> NameScope::Find(std::wstring_view name) const
    {
        if (m_thread != std::this_thread::get_id()) { throw winrt::hresult_wrong_thread(); }
        auto found = m_names.find(name);
        return found == m_names.end() ? nullptr : found->second.get();
    }
}
