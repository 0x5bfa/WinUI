module Xaml.Runtime;

import std;
import winrt_base;

namespace xaml::runtime
{
    PropertyBinding::PropertyBinding(DependencyObject& source, DependencyPropertyHandle sourceProperty,
        DependencyObject& target, DependencyPropertyHandle targetProperty,
        BindingMode mode, Converter convert, Converter convertBack, bool templateValue)
        : m_source(source.get_weak()), m_target(target.get_weak()),
          m_sourceProperty(std::move(sourceProperty)), m_targetProperty(std::move(targetProperty)),
          m_mode(mode), m_convert(std::move(convert)), m_convertBack(std::move(convertBack)), m_template(templateValue)
    {
    }

    std::shared_ptr<PropertyBinding> PropertyBinding::Connect(DependencyObject& source,
        DependencyPropertyHandle sourceProperty, DependencyObject& target, DependencyPropertyHandle targetProperty,
        BindingMode mode, Converter convert, Converter convertBack, bool templateValue)
    {
        source.VerifyAccess(); target.VerifyAccess();
        if (mode < BindingMode::OneWay || mode > BindingMode::OneTime) { throw winrt::hresult_invalid_argument(); }
        if (&source == &target && sourceProperty.Id() == targetProperty.Id()) { throw winrt::hresult_invalid_argument(); }
        auto binding = std::shared_ptr<PropertyBinding>(new PropertyBinding(source, std::move(sourceProperty),
            target, std::move(targetProperty), mode, std::move(convert), std::move(convertBack), templateValue));
        std::weak_ptr<PropertyBinding> weak = binding;
        try
        {
            binding->m_expressionToken = target.AttachExpression(binding->m_targetProperty,
                [binding] { binding->Disconnect(); }, templateValue);
            if (mode != BindingMode::OneTime)
            {
                binding->m_sourceToken = source.RegisterPropertyChangedCallback(binding->m_sourceProperty,
                    [weak](auto&, auto const&) { if (auto owner = weak.lock()) { owner->UpdateTarget(); } });
            }
            if (mode == BindingMode::TwoWay)
            {
                binding->m_targetToken = target.RegisterPropertyChangedCallback(binding->m_targetProperty,
                    [weak](auto&, auto const&) { if (auto owner = weak.lock()) { owner->UpdateSource(); } });
            }
            binding->UpdateTarget();
        }
        catch (...) { binding->Disconnect(); throw; }
        return binding;
    }

    PropertyBinding::~PropertyBinding()
    {
        try { Disconnect(); } catch (...) {}
    }

    void PropertyBinding::UpdateTarget()
    {
        if (!m_active) { return; }
        if (m_updating) { m_pending = true; return; }
        auto source = m_source.get(); auto target = m_target.get();
        if (!source || !target) { Disconnect(); return; }
        source->VerifyAccess(); target->VerifyAccess();
        m_updating = true;
        try
        {
            for (int pass = 0; pass != 16; ++pass)
            {
                m_pending = false;
                auto value = source->GetValue(m_sourceProperty);
                if (m_convert) { value = m_convert(value); }
                if (!m_active) { break; }
                target->SetExpressionValue(m_targetProperty, m_expressionToken, std::move(value), m_template);
                if (!m_pending || !m_active) { m_updating = false; return; }
            }
            m_updating = false;
            if (m_pending && m_active) { throw winrt::hresult_illegal_method_call(); }
        }
        catch (...) { m_updating = false; throw; }
    }

    void PropertyBinding::UpdateSource()
    {
        if (!m_active || m_updating || m_mode != BindingMode::TwoWay) { return; }
        auto source = m_source.get(); auto target = m_target.get();
        if (!source || !target) { Disconnect(); return; }
        m_updating = true;
        try
        {
            auto value = target->GetValue(m_targetProperty);
            if (m_convertBack) { value = m_convertBack(value); }
            if (m_active) { source->SetValue(m_sourceProperty, std::move(value)); }
            m_updating = false;
            if (m_pending && m_active) { UpdateTarget(); }
        }
        catch (...) { m_updating = false; throw; }
    }

    void PropertyBinding::Disconnect()
    {
        if (!m_active) { return; }
        auto source = m_source.get(); auto target = m_target.get();
        if (source) { source->VerifyAccess(); }
        if (target) { target->VerifyAccess(); }
        m_active = false;
        if (source && m_sourceToken) { source->UnregisterPropertyChangedCallback(m_sourceProperty, m_sourceToken); }
        if (target && m_targetToken) { target->UnregisterPropertyChangedCallback(m_targetProperty, m_targetToken); }
        if (target && m_expressionToken) { target->DetachExpression(m_targetProperty, m_expressionToken, m_template); }
    }
}
