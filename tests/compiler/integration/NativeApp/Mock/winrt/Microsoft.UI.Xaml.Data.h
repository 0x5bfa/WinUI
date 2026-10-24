#pragma once
#include "Microsoft.UI.Xaml.h"

namespace winrt::Microsoft::UI::Xaml::Data
{
    struct INotifyPropertyChanged
    {
        struct State
        {
            int64_t nextToken{};
            std::map<int64_t, std::function<void()>> callbacks;
        };
        std::shared_ptr<State> state;
        INotifyPropertyChanged() : state(std::make_shared<State>()) {}
        INotifyPropertyChanged(std::nullptr_t) {}
        explicit operator bool() const { return static_cast<bool>(state); }
        template <typename F> winrt::event_token PropertyChanged(F&& callback) const
        {
            auto token = ++state->nextToken;
            state->callbacks[token] = [callback = std::forward<F>(callback)]() { callback(0, 0); };
            return { token };
        }
        void PropertyChanged(winrt::event_token token) const { state->callbacks.erase(token.value); }
        size_t SubscriptionCount() const { return state->callbacks.size(); }
        void RaiseChanged() const
        {
            std::vector<std::function<void()>> callbacks;
            for (auto const& [token, callback] : state->callbacks) callbacks.push_back(callback);
            for (auto const& callback : callbacks) callback();
        }
    };
}
