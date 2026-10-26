module Xaml.Runtime;

import std;
import winrt_base;

namespace xaml::runtime
{
    ModelBinding::ModelBinding(std::shared_ptr<ObservableModel> source,
        std::vector<std::wstring> segments, DependencyObject& target,
        DependencyPropertyHandle property, BindingMode mode)
        : m_source(std::move(source)), m_segments(std::move(segments)),
          m_target(target.get_weak()), m_property(std::move(property)), m_mode(mode) {}

    std::shared_ptr<ModelBinding> ModelBinding::Connect(std::shared_ptr<ObservableModel> source,
        std::wstring path, DependencyObject& target, DependencyPropertyHandle property, BindingMode mode)
    {
        target.VerifyAccess();
        if (!source || path.empty() || mode < BindingMode::OneWay || mode > BindingMode::OneTime ||
            !property.IsValidFor(target)) { throw winrt::hresult_invalid_argument(); }
        std::vector<std::wstring> segments;
        for (std::size_t start = 0; start < path.length();)
        {
            auto const end = path.find(L'.', start);
            auto const length = end == std::wstring::npos ? path.size() - start : end - start;
            if (length == 0) { throw winrt::hresult_invalid_argument(); }
            segments.push_back(path.substr(start, length));
            if (end == std::wstring::npos) { break; }
            start = end + 1;
            if (start == path.size()) { throw winrt::hresult_invalid_argument(); }
        }
        auto binding = std::shared_ptr<ModelBinding>(new ModelBinding(
            std::move(source), std::move(segments), target, std::move(property), mode));
        try
        {
            binding->m_expressionToken = target.AttachExpression(binding->m_property,
                [binding] { binding->Disconnect(); });
            if (mode == BindingMode::TwoWay)
            {
                auto weak = std::weak_ptr<ModelBinding>(binding);
                binding->m_targetToken = target.RegisterPropertyChangedCallback(binding->m_property,
                    [weak](auto&, auto const&)
                    { if (auto owner = weak.lock()) { owner->UpdateSource(); } });
            }
            binding->UpdateTarget();
        }
        catch (...) { binding->Disconnect(); throw; }
        return binding;
    }

    ModelBinding::~ModelBinding()
    {
        try { Disconnect(); } catch (...) {}
    }

    void ModelBinding::UnsubscribeSources()
    {
        auto sources = std::move(m_subscriptions);
        m_subscriptions.clear();
        for (auto& item : sources) { item.model->Unsubscribe(item.token); }
    }

    std::pair<std::shared_ptr<ObservableModel>, std::any> ModelBinding::ObservePath()
    {
        UnsubscribeSources();
        auto current = m_source;
        std::any value;
        for (std::size_t i = 0; i < m_segments.size(); ++i)
        {
            if (!current) { throw winrt::hresult_invalid_argument(); }
            if (m_mode != BindingMode::OneTime)
            {
                std::weak_ptr<ModelBinding> weak = weak_from_this();
                auto const name = m_segments[i];
                auto const token = current->Subscribe([weak, name](std::wstring_view changed)
                {
                    if ((changed.empty() || changed == name))
                    {
                        if (auto owner = weak.lock()) { owner->UpdateTarget(); }
                    }
                });
                m_subscriptions.push_back({ current, token });
            }
            value = current->GetProperty(m_segments[i]);
            if (i + 1 < m_segments.size())
            {
                auto const nested = std::any_cast<std::shared_ptr<ObservableModel>>(&value);
                if (!nested) { throw winrt::hresult_invalid_argument(); }
                current = *nested;
            }
        }
        return { current, std::move(value) };
    }

    void ModelBinding::UpdateTarget()
    {
        if (!m_active) { return; }
        if (m_updating) { m_pending = true; return; }
        auto target = m_target.get();
        if (!target) { Disconnect(); return; }
        target->VerifyAccess();
        auto lifetime = shared_from_this();
        m_updating = true;
        try
        {
            for (int pass = 0; pass < 16 && m_active; ++pass)
            {
                m_pending = false;
                auto [source, value] = ObservePath();
                if (!m_active) { break; }
                target->SetExpressionValue(m_property, m_expressionToken, std::move(value));
                if (!m_pending) { m_updating = false; return; }
            }
            m_updating = false;
            if (m_pending && m_active) { throw winrt::hresult_illegal_method_call(); }
        }
        catch (...) { m_updating = false; throw; }
    }

    void ModelBinding::UpdateSource()
    {
        if (!m_active || m_mode != BindingMode::TwoWay) { return; }
        if (m_updating) { m_pending = true; return; }
        auto target = m_target.get();
        if (!target) { Disconnect(); return; }
        target->VerifyAccess();
        m_updating = true;
        try
        {
            auto [source, value] = ObservePath();
            source->SetProperty(m_segments.back(), target->GetValue(m_property));
            m_updating = false;
            if (m_pending) { UpdateTarget(); }
        }
        catch (...) { m_updating = false; throw; }
    }

    void ModelBinding::Disconnect()
    {
        if (!m_active) { return; }
        auto target = m_target.get();
        if (target) { target->VerifyAccess(); }
        m_active = false;
        UnsubscribeSources();
        if (target && m_targetToken) { target->UnregisterPropertyChangedCallback(m_property, m_targetToken); }
        if (target && m_expressionToken) { target->DetachExpression(m_property, m_expressionToken); }
        m_source.reset();
    }
}
