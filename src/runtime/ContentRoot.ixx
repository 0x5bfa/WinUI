export module Xaml.Runtime:ContentRoot;

import std;
import winrt_base;
import winrt.Microsoft.UI.Dispatching;
import :Primitives;
import :Input;

export namespace xaml::runtime
{
    // Owns one live visual root. The host supplies the UI dispatcher enqueue
    // operation and a render callback; neither is a second object model.
    class ContentRoot final : public std::enable_shared_from_this<ContentRoot>
    {
    public:
        using Enqueue = std::function<bool(std::function<void()>)>;
        using Render = std::function<void(UIElement&)>;

        static std::shared_ptr<ContentRoot> Create(winrt::com_ptr<UIElement> element,
            Size viewport, Enqueue dispatcher, Render renderer);
        // Attaches to an existing Windows App SDK DispatcherQueue on this UI thread.
        static std::shared_ptr<ContentRoot> CreateOnCurrentDispatcher(
            winrt::com_ptr<UIElement> element, Size viewport, Render renderer);
        ~ContentRoot();

        ContentRoot(ContentRoot const&) = delete;
        ContentRoot& operator=(ContentRoot const&) = delete;

        void Resize(Size viewport);
        void SetRasterizationScale(double scale);
        [[nodiscard]] double RasterizationScale() const;
        void Invalidate();
        // Flush a pending layout/render frame synchronously on the owning UI thread.
        void UpdateLayout();
        void ProcessFrame();
        void Shutdown();
        [[nodiscard]] InputManager& Input();
        [[nodiscard]] bool IsActive() const noexcept { return m_active; }
        [[nodiscard]] std::uint64_t FrameCount() const noexcept { return m_frames; }

    private:
        ContentRoot(winrt::com_ptr<UIElement> element, Size viewport, Enqueue dispatcher, Render renderer);
        void QueueFrame();

        winrt::com_ptr<UIElement> m_element;
        Size m_viewport;
        double m_scale{ 1.0 };
        Enqueue m_enqueue;
        Render m_renderer;
        std::thread::id m_thread;
        bool m_active{ true };
        bool m_queued{};
        bool m_processing{};
        std::uint64_t m_frames{};
        std::uint64_t m_invalidationVersion{};
        std::shared_ptr<InputManager> m_input;
    };
}
