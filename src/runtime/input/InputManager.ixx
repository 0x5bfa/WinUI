export module Xaml.Runtime:Input;

import std;
import winrt_base;
import :Primitives;

export namespace xaml::runtime
{
    // One input state owner per content root. References to targets are weak;
    // a synchronous route temporarily retains its elements across callbacks.
    class InputManager final
    {
    public:
        explicit InputManager(UIElement& root);
        InputManager(InputManager const&) = delete;
        InputManager& operator=(InputManager const&) = delete;

        bool Process(InputEventArgs args);
        bool Focus(UIElement* element);
        [[nodiscard]] winrt::com_ptr<UIElement> FocusedElement() const;
        bool CapturePointer(UIElement& element, std::uint32_t pointerId);
        void ReleasePointer(std::uint32_t pointerId);
        [[nodiscard]] winrt::com_ptr<UIElement> CapturedElement(std::uint32_t pointerId) const;
        void ValidateState();
        void CancelInput();
        void Shutdown();

    private:
        using WeakElement = winrt::weak_ref<DependencyObject>;
        using Route = std::vector<winrt::com_ptr<UIElement>>;
        static winrt::com_ptr<UIElement> Resolve(WeakElement const& weak);
        [[nodiscard]] bool Contains(UIElement const& element) const;
        [[nodiscard]] bool CanReceiveInput(UIElement const& element) const;
        [[nodiscard]] Route SnapshotRoute(UIElement& target) const;
        bool Raise(UIElement& target, InputEventArgs& args, bool bubble = true);
        void UpdatePointerOver(winrt::com_ptr<UIElement> target, InputEventArgs const& input);
        void VerifyAccess() const;

        std::thread::id m_thread;
        WeakElement m_root;
        WeakElement m_focus;
        std::unordered_map<std::uint32_t, WeakElement> m_capture;
        std::unordered_map<std::uint32_t, WeakElement> m_over;
        std::unordered_set<std::uint32_t> m_pressed;
        bool m_active{ true };
        bool m_resetting{};
        bool m_validating{};
        std::uint64_t m_cancellationVersion{};
    };
}
