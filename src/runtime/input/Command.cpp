module Xaml.Runtime;

import std;
import winrt_base;

namespace xaml::runtime
{
    Command::Command(ExecuteHandler execute, Predicate canExecute)
        : m_thread(std::this_thread::get_id()), m_execute(std::move(execute)), m_canExecute(std::move(canExecute))
    {
        if (!m_execute) { throw winrt::hresult_invalid_argument(); }
    }
    void Command::VerifyAccess() const
    { if (m_thread != std::this_thread::get_id()) { throw winrt::hresult_wrong_thread(); } }
    bool Command::CanExecute(Parameter const& parameter) const
    { VerifyAccess(); auto lifetime = weak_from_this().lock(); auto predicate = m_canExecute; return !predicate || predicate(parameter); }
    void Command::Execute(Parameter const& parameter)
    { VerifyAccess(); auto lifetime = weak_from_this().lock(); auto execute = m_execute; if (CanExecute(parameter)) { execute(parameter); } }
    std::uint64_t Command::Subscribe(std::function<void()> handler)
    {
        VerifyAccess();
        if (!handler) { throw winrt::hresult_invalid_argument(); }
        if (m_nextToken == std::numeric_limits<std::uint64_t>::max()) { throw winrt::hresult_out_of_bounds(); }
        auto token = ++m_nextToken;
        m_subscriptions.push_back(std::make_shared<Subscription>(token, std::move(handler))); return token;
    }
    void Command::Unsubscribe(std::uint64_t token)
    {
        VerifyAccess();
        auto found = std::ranges::find_if(m_subscriptions, [=](auto const& item) { return item->token == token; });
        if (found == m_subscriptions.end()) { return; }
        auto removed = *found; removed->active = false; m_subscriptions.erase(found);
    }
    void Command::NotifyCanExecuteChanged()
    {
        VerifyAccess();
        auto lifetime = weak_from_this().lock();
        if (m_notifying) { m_pending = true; return; }
        m_notifying = true;
        try
        {
            for (int pass = 0; pass < 16; ++pass)
            {
                m_pending = false; auto subscriptions = m_subscriptions;
                for (auto const& item : subscriptions) { if (item->active) { item->handler(); } }
                if (!m_pending) { m_notifying = false; return; }
            }
            throw winrt::hresult_illegal_method_call();
        }
        catch (...) { m_notifying = false; throw; }
    }
}
