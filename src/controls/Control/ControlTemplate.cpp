module Xaml.Controls;

import std;
import winrt_base;
import Xaml.Runtime;

namespace xaml::controls
{
    runtime::DependencyProperty<std::shared_ptr<ControlTemplate>> const Control::TemplateProperty =
        runtime::DependencyProperty<std::shared_ptr<ControlTemplate>>::Register<Control>("Template", {}, runtime::PropertyEffects::Measure);
    namespace
    {
        std::atomic<std::uint64_t> nextStateToken{};
        void SetTemplateOwner(runtime::UIElement& element, runtime::FrameworkElement* owner, runtime::FrameworkElement* previous = nullptr)
        {
            if (auto framework = dynamic_cast<runtime::FrameworkElement*>(&element))
            {
                auto current = framework->TemplatedParent();
                if (current && current.get() != owner && current.get() != previous) { return; }
                framework->TemplatedParent(owner);
            }
            for (std::size_t i = 0; i < element.VisualChildrenCount(); ++i)
            {
                SetTemplateOwner(*element.VisualChildAt(i), owner, previous);
            }
        }
    }

    ControlTemplate::ControlTemplate(Factory factory) : m_factory(std::move(factory))
    {
        if (!m_factory) { throw winrt::hresult_invalid_argument(); }
    }
    TemplateInstance ControlTemplate::Instantiate(Control& owner) const
    {
        owner.VerifyAccess();
        auto result = m_factory(owner);
        if (!result.root || result.root.get() == &owner || !result.names || result.root->Parent() || result.root->IsConnectedToRoot())
        {
            throw winrt::hresult_invalid_argument();
        }
        std::unordered_set<std::wstring> groups, states;
        for (auto const& group : result.states)
        {
            if (group.name.empty() || !groups.insert(group.name).second) { throw winrt::hresult_invalid_argument(); }
            for (auto const& state : group.states)
            { if (state.name.empty() || !states.insert(state.name).second) { throw winrt::hresult_invalid_argument(); } }
        }
        if (std::ranges::any_of(result.bindings, [](auto const& binding) { return !binding; }))
        { throw winrt::hresult_invalid_argument(); }
        return result;
    }

    Control::~Control()
    {
        for (auto const& [_, application] : m_states)
        {
            for (auto const& [weak, property] : application.targets)
            { try { if (auto target = weak.get()) { target->ClearStateValue(property, application.token); } } catch (...) {} }
        }
        // Targets retain no strong reference back to their templated parent.
        if (m_instance)
        {
            for (auto const& binding : m_instance->bindings) { try { binding->Disconnect(); } catch (...) {} }
        }
    }
    void Control::Template(std::shared_ptr<ControlTemplate> value)
    {
        SetValue(TemplateProperty, std::move(value));
    }
    std::shared_ptr<ControlTemplate> Control::Template() const { return GetValue(TemplateProperty); }
    bool Control::HasTemplateRoot() const { VerifyAccess(); return m_instance.has_value(); }
    void Control::ClearTemplate()
    {
        if (m_clearingTemplate) { return; }
        m_clearingTemplate = true;
        std::exception_ptr error;
        try { if (m_instance) { OnTemplateClearing(); } } catch (...) { error = std::current_exception(); }
        auto previous = std::exchange(m_instance, {});
        auto states = std::exchange(m_states, {});
        auto attempt = [&](auto&& operation)
        {
            try { operation(); } catch (...) { if (!error) { error = std::current_exception(); } }
        };
        for (auto const& [_, application] : states)
        {
            for (auto const& [weak, property] : application.targets)
            {
                attempt([&] { if (auto target = weak.get()) { target->ClearStateValue(property, application.token); } });
            }
        }
        if (previous)
        {
            for (auto const& binding : previous->bindings) { attempt([&] { binding->Disconnect(); }); }
            attempt([&] { SetTemplateOwner(*previous->root, nullptr, this); });
            attempt([&] { if (VisualChildrenCount() && VisualChildAt(0) == previous->root) { SetSingleVisualChild(nullptr); } });
        }
        m_clearingTemplate = false;
        if (error) { std::rethrow_exception(error); }
    }
    bool Control::ApplyTemplate()
    {
        VerifyAccess();
        auto lifetime = get_strong();
        if (m_applyingTemplate || m_clearingTemplate) { return false; }
        RefreshDefaultStyle();
        auto selected = Template();
        if (m_instance || !selected) { return false; }
        m_applyingTemplate = true;
        auto version = m_templateVersion;
        try
        {
            auto instance = selected->Instantiate(*this);
            if (version != m_templateVersion)
            {
                for (auto const& binding : instance.bindings) { binding->Disconnect(); }
                m_applyingTemplate = false;
                return false;
            }
            SetTemplateOwner(*instance.root, this);
            if (auto element = dynamic_cast<runtime::FrameworkElement*>(instance.root.get()))
            {
                element->SetNameScope(instance.names);
            }
            // Commit the new instance before callbacks can request its parts.
            m_instance = std::move(instance);
            SetSingleVisualChild(m_instance->root.get());
            if (version == m_templateVersion) { OnApplyTemplate(); }
            if (version == m_templateVersion) { UpdateVisualState(); }
            m_applyingTemplate = false;
            return version == m_templateVersion && m_instance.has_value();
        }
        catch (...)
        {
            m_applyingTemplate = false;
            if (version == m_templateVersion) { ClearTemplate(); }
            throw;
        }
    }
    winrt::com_ptr<runtime::DependencyObject> Control::GetTemplateChild(std::wstring_view name) const
    {
        VerifyAccess();
        return m_instance ? m_instance->names->Find(name) : nullptr;
    }
    bool Control::GoToState(std::wstring_view name)
    {
        VerifyAccess();
        auto lifetime = get_strong();
        if (!m_instance) { return false; }
        auto instance = *m_instance;
        for (auto const& group : instance.states)
        {
            auto selected = std::ranges::find_if(group.states, [&](auto const& state) { return state.name == name; });
            if (selected == group.states.end()) { continue; }
            auto found = m_states.find(group.name);
            if (found != m_states.end() && found->second.state == name) { return true; }
            struct Target { winrt::com_ptr<runtime::DependencyObject> object; runtime::Setter setter; };
            std::vector<Target> targets;
            for (auto const& entry : selected->setters)
            {
                auto target = entry.targetName.empty() ? runtime::DependencyObject::get_strong() : instance.names->Find(entry.targetName);
                if (!target) { throw winrt::hresult_invalid_argument(); }
                target->ValidateValue(entry.setter.property, entry.setter.value);
                targets.push_back({ std::move(target), entry.setter });
            }
            auto const version = m_templateVersion;
            auto previous = found == m_states.end() ? StateApplication{} : std::move(found->second);
            auto token = ++nextStateToken;
            m_states.insert_or_assign(group.name, StateApplication{ std::wstring(name), token });
            auto cleanup = [](StateApplication const& application)
            {
                std::exception_ptr error;
                for (auto const& [weak, property] : application.targets)
                {
                    try { if (auto target = weak.get()) { target->ClearStateValue(property, application.token); } }
                    catch (...) { if (!error) { error = std::current_exception(); } }
                }
                if (error) { std::rethrow_exception(error); }
            };
            auto current = [&]
            {
                auto active = m_states.find(group.name);
                return version == m_templateVersion && active != m_states.end() && active->second.token == token;
            };
            try
            {
                for (auto const& target : targets)
                {
                    if (!current()) { break; }
                    m_states.at(group.name).targets.emplace_back(target.object->get_weak(), target.setter.property);
                    target.object->SetStateValue(target.setter.property, token, target.setter.value);
                }
            }
            catch (...)
            {
                auto error = std::current_exception();
                if (current())
                {
                    auto failed = std::move(m_states.at(group.name));
                    m_states.erase(group.name);
                    try { cleanup(failed); } catch (...) {}
                }
                try { cleanup(previous); } catch (...) {}
                std::rethrow_exception(error);
            }
            cleanup(previous);
            return current();
        }
        return false;
    }
    void Control::DefaultStyleKey(std::wstring key)
    {
        VerifyAccess(); m_defaultStyleKey = std::move(key); WatchResources(); RefreshDefaultStyle();
    }
    std::wstring_view Control::DefaultStyleKey() const { VerifyAccess(); return m_defaultStyleKey; }
    void Control::RefreshDefaultStyle()
    {
        WatchResources();
        if (m_applyingDefaultStyle || m_defaultStyleKey.empty() || (StyleValue() && StyleValue() != m_defaultStyle)) { return; }
        auto value = TryFindResource(m_defaultStyleKey);
        auto selected = value ? std::any_cast<std::shared_ptr<runtime::Style>>(*value) : nullptr;
        if (selected == m_defaultStyle) { return; }
        m_applyingDefaultStyle = true;
        m_defaultStyle = selected;
        try { StyleValue(std::move(selected)); m_applyingDefaultStyle = false; }
        catch (...) { m_applyingDefaultStyle = false; throw; }
    }
    void Control::OnResourcesChanged() { RefreshDefaultStyle(); UpdateVisualState(); }
    void Control::OnStyleChanged() { if (!m_applyingDefaultStyle) { m_defaultStyle.reset(); } }
    void Control::OnLoading() { (void)ApplyTemplate(); }
    runtime::Size Control::MeasureOverride(runtime::Size available)
    {
        (void)ApplyTemplate();
        if (!m_instance) { return {}; }
        auto child = m_instance->root;
        child->Measure(available);
        return child->DesiredSize();
    }
    runtime::Size Control::ArrangeOverride(runtime::Size finalSize)
    {
        if (m_instance) { m_instance->root->Arrange({ 0, 0, finalSize.width, finalSize.height }); }
        return finalSize;
    }
    void Control::UpdateVisualState() { (void)GoToState(IsFocused() ? L"Focused" : L"Unfocused"); }
}
