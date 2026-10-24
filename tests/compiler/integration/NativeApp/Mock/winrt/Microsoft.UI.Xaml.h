#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <utility>
#include <vector>

namespace winrt::Windows::Foundation
{
    struct Uri
    {
        explicit Uri(wchar_t const*) {}
    };

    struct IInspectable
    {
        template <typename T> T as() const { return T{}; }
    };
}

namespace winrt
{
    struct event_token { int64_t value{}; };
}

namespace winrt::Microsoft::UI::Xaml
{
    struct DependencyProperty {};
    struct DependencyState
    {
        int64_t nextToken{};
        std::map<int64_t, std::function<void()>> callbacks;
    };
    struct DependencyObject
    {
        std::shared_ptr<DependencyState> state;
        DependencyObject() : state(std::make_shared<DependencyState>()) {}
        DependencyObject(std::nullptr_t) {}
        explicit DependencyObject(std::shared_ptr<DependencyState> value) : state(std::move(value)) {}
        explicit operator bool() const { return static_cast<bool>(state); }
        template <typename F> int64_t RegisterPropertyChangedCallback(DependencyProperty const&, F&& callback) const
        {
            auto token = ++state->nextToken;
            state->callbacks[token] = [callback = std::forward<F>(callback), instance = *this]()
            {
                callback(instance, DependencyProperty{});
            };
            return token;
        }
        void UnregisterPropertyChangedCallback(DependencyProperty const&, int64_t token) const
        {
            state->callbacks.erase(token);
        }
        void RaiseChanged() const
        {
            std::vector<std::function<void()>> callbacks;
            for (auto const& [token, callback] : state->callbacks) callbacks.push_back(callback);
            for (auto const& callback : callbacks) callback();
        }
    };

    struct Application
    {
        template <typename T> static void LoadComponent(T& page, winrt::Windows::Foundation::Uri const&)
        {
            for (int id = 1; id <= 4; ++id) page.Connect(id, {});
        }
    };
}

namespace winrt::Fixture::implementation
{
    template <typename D, typename... I> struct MainPage_base
    {
        std::function<void()> onLoaded;
        std::function<void()> onUnloaded;
        template <typename F> void Loaded(F&& callback)
        {
            onLoaded = [callback = std::forward<F>(callback)]() { callback(0, 0); };
        }
        template <typename F> void Unloaded(F&& callback)
        {
            onUnloaded = [callback = std::forward<F>(callback)]() { callback(0, 0); };
        }
        void RaiseLoaded() const { if (onLoaded) onLoaded(); }
        void RaiseUnloaded() const { if (onUnloaded) onUnloaded(); }
    };
}
