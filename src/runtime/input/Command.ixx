export module Xaml.Runtime:Command;

import std;
import winrt_base;
import winrt.Windows.Foundation;

export namespace xaml::runtime
{
    // Native command contract; this is not the projected WinRT ICommand ABI.
    class Command final : public std::enable_shared_from_this<Command>
    {
    public:
        using Parameter = winrt::Windows::Foundation::IInspectable;
        using ExecuteHandler = std::function<void(Parameter const&)>;
        using Predicate = std::function<bool(Parameter const&)>;
        explicit Command(ExecuteHandler execute, Predicate canExecute = {});
        [[nodiscard]] bool CanExecute(Parameter const& parameter) const;
        void Execute(Parameter const& parameter);
        std::uint64_t Subscribe(std::function<void()> handler);
        void Unsubscribe(std::uint64_t token);
        void NotifyCanExecuteChanged();
    private:
        void VerifyAccess() const;
        struct Subscription { std::uint64_t token; std::function<void()> handler; bool active{ true }; };
        std::thread::id m_thread;
        ExecuteHandler m_execute;
        Predicate m_canExecute;
        std::vector<std::shared_ptr<Subscription>> m_subscriptions;
        std::uint64_t m_nextToken{};
        bool m_notifying{}, m_pending{};
    };
}
