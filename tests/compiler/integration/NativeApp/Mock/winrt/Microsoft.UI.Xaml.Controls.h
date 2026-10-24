#pragma once
#include "Microsoft.UI.Xaml.h"
#include <string>

namespace winrt::Microsoft::UI::Xaml::Controls
{
    struct Page
    {
        Page(std::nullptr_t = nullptr) {}
        explicit operator bool() const { return true; }
    };

    struct Grid
    {
        Grid(std::nullptr_t = nullptr) {}
        explicit operator bool() const { return true; }
    };

    struct TextBlock
    {
        struct State
        {
            std::wstring text;
            ::winrt::Microsoft::UI::Xaml::DependencyObject dependency;
        };
        std::shared_ptr<State> state;
        TextBlock() : state(std::make_shared<State>()) {}
        TextBlock(std::nullptr_t) {}
        explicit operator bool() const { return static_cast<bool>(state); }
        template <typename T> T try_as() const { return static_cast<T>(state->dependency); }
        static ::winrt::Microsoft::UI::Xaml::DependencyProperty TextProperty() { return {}; }
        std::wstring Text() const { return state->text; }
        size_t CallbackCount() const { return state->dependency.state->callbacks.size(); }
        void Text(std::wstring const& value)
        {
            state->text = value;
            state->dependency.RaiseChanged();
        }
    };

    struct EventHandler
    {
        std::function<void()> invoke;
        template <typename D, typename M> EventHandler(D* page, M method)
            : invoke([page, method]() { (page->*method)(); }) {}
    };

    struct Button
    {
        std::shared_ptr<std::function<void()>> clicked;
        Button() : clicked(std::make_shared<std::function<void()>>()) {}
        Button(std::nullptr_t) {}
        explicit operator bool() const { return static_cast<bool>(clicked); }
        void Click(EventHandler handler) { *clicked = std::move(handler.invoke); }
        void RaiseClick() const { if (clicked && *clicked) (*clicked)(); }
    };
}
