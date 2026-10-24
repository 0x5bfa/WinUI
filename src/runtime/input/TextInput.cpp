module;
#include "TextServicesBackend.h"

module Xaml.Runtime;

import std;
import winrt_base;

namespace xaml::runtime
{
    TextInputSession::TextInputSession(std::uintptr_t window)
        : m_thread(std::this_thread::get_id()), m_backend(XamlTextServicesCreate(window)) {}
    TextInputSession::~TextInputSession()
    {
        if (m_backend && m_thread == std::this_thread::get_id()) { Shutdown(); }
    }
    void TextInputSession::VerifyAccess() const
    {
        if (m_thread != std::this_thread::get_id()) { throw winrt::hresult_wrong_thread(); }
        if (!m_backend) { throw winrt::hresult_illegal_method_call(); }
    }
    void TextInputSession::Focus(UIElement* element)
    {
        VerifyAccess();
        m_requestedFocus = element ? static_cast<DependencyObject&>(*element).get_weak() : winrt::weak_ref<DependencyObject>{};
        ++m_focusVersion;
        if (m_changingFocus) { return; }
        m_changingFocus = true;
        try
        {
            for (int pass = 0; pass < 16; ++pass)
            {
                auto version = m_focusVersion;
                auto requested = m_requestedFocus.get();
                FocusCore(dynamic_cast<UIElement*>(requested.get()));
                if (!m_backend) { m_changingFocus = false; return; }
                if (version == m_focusVersion) { m_changingFocus = false; return; }
            }
            throw winrt::hresult_illegal_method_call();
        }
        catch (...) { m_changingFocus = false; throw; }
    }
    void TextInputSession::FocusCore(UIElement* element)
    {
        VerifyAccess();
        if (element) { element->VerifyAccess(); }
        if ((element && m_client.get().get() == element) || (!element && !m_hasClient)) { return; }
        m_client = element && dynamic_cast<TextInputClient*>(element) ?
            static_cast<DependencyObject&>(*element).get_weak() : winrt::weak_ref<DependencyObject>{};
        m_hasClient = m_client.get() != nullptr;
        if (!m_hasClient) { XamlTextServicesFocus(m_backend, {}); return; }
        auto weak = m_client;
        auto invoke = [weak](auto&& action) -> decltype(auto)
        {
            auto object = weak.get();
            if (!object) { throw winrt::hresult_illegal_method_call(); }
            object->VerifyAccess();
            return action(*dynamic_cast<TextInputClient*>(object.get()));
        };
        platform::TextCallbacks callbacks;
        callbacks.text = [invoke] { return invoke([](auto& client) { return client.TextContent(); }); };
        callbacks.selection = [invoke]
        {
            return invoke([](auto& client) { auto value = client.Selection(); return platform::TextSelection{ value.start, value.end, value.activeAtStart }; });
        };
        callbacks.select = [invoke](platform::TextSelection value)
        {
            invoke([&](auto& client) { client.Selection({ value.start, value.end, value.activeAtStart }); });
        };
        callbacks.replace = [invoke](std::uint32_t start, std::uint32_t end, std::wstring_view text)
        {
            invoke([&](auto& client) { client.ReplaceText(start, end, text); });
        };
        callbacks.bounds = [invoke](std::uint32_t start, std::uint32_t end)
        {
            return invoke([&](auto& client) { auto value = client.TextBounds(start, end); return platform::TextBounds{ value.x, value.y, value.width, value.height }; });
        };
        callbacks.position = [invoke](double x, double y)
        {
            return invoke([&](auto& client) { return client.TextPosition({ x, y }); });
        };
        callbacks.readOnly = [invoke] { return invoke([](auto& client) { return client.IsReadOnly(); }); };
        callbacks.composition = [invoke](std::uint32_t start, std::uint32_t end, bool active)
        {
            invoke([&](auto& client) { client.CompositionChanged(start, end, active); });
        };
        try { XamlTextServicesFocus(m_backend, std::move(callbacks)); }
        catch (...) { m_client = {}; m_hasClient = false; throw; }
    }
    void TextInputSession::NotifyTextChanged(UIElement& element, std::uint32_t start, std::uint32_t oldEnd, std::uint32_t newEnd)
    {
        VerifyAccess(); if (m_client.get().get() == &element) { XamlTextServicesChanged(m_backend, start, oldEnd, newEnd); }
    }
    void TextInputSession::NotifySelectionChanged(UIElement& element)
    {
        VerifyAccess(); if (m_client.get().get() == &element) { XamlTextServicesSelectionChanged(m_backend); }
    }
    void TextInputSession::NotifyLayoutChanged(double scale)
    {
        VerifyAccess(); XamlTextServicesLayoutChanged(m_backend, scale);
    }
    bool TextInputSession::PreTranslateMessage(std::uint32_t message, std::uintptr_t wparam, std::intptr_t lparam)
    {
        VerifyAccess(); return XamlTextServicesKey(m_backend, message, wparam, lparam);
    }
    void TextInputSession::Shutdown()
    {
        VerifyAccess(); m_client = {}; XamlTextServicesDestroy(std::exchange(m_backend, nullptr));
    }
}
