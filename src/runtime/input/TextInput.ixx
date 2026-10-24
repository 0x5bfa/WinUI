export module Xaml.Runtime:TextInput;

import std;
import winrt_base;
import :Primitives;

export namespace xaml::runtime
{
    class TextInputSession final
    {
    public:
        explicit TextInputSession(std::uintptr_t inputWindow);
        ~TextInputSession();
        TextInputSession(TextInputSession const&) = delete;
        TextInputSession& operator=(TextInputSession const&) = delete;
        void Focus(UIElement* element);
        void NotifyTextChanged(UIElement& element, std::uint32_t start, std::uint32_t oldEnd, std::uint32_t newEnd);
        void NotifySelectionChanged(UIElement& element);
        void NotifyLayoutChanged(double scale);
        bool PreTranslateMessage(std::uint32_t message, std::uintptr_t wparam, std::intptr_t lparam);
        void Shutdown();
    private:
        void VerifyAccess() const;
        void FocusCore(UIElement* element);
        std::thread::id m_thread;
        void* m_backend{};
        winrt::weak_ref<DependencyObject> m_client;
        winrt::weak_ref<DependencyObject> m_requestedFocus;
        std::uint64_t m_focusVersion{};
        bool m_changingFocus{}, m_hasClient{};
    };
}
