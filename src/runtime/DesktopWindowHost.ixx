export module Xaml.Runtime:Hosting;

import std;
import winrt_base;
import :Primitives;
import :ContentRoot;
import :Rendering;

export namespace xaml::runtime
{
    // Attaches a runtime root to a caller-owned HWND identified by WindowId.
    // The caller owns its message loop and Windows App SDK initialization.
    class DesktopWindowHost final
    {
    public:
        static std::shared_ptr<DesktopWindowHost> CreateForWindow(std::uint64_t windowId,
            winrt::com_ptr<UIElement> element, int widthPixels, int heightPixels,
            double rasterizationScale = 1.0);
        ~DesktopWindowHost();
        DesktopWindowHost(DesktopWindowHost const&) = delete;
        DesktopWindowHost& operator=(DesktopWindowHost const&) = delete;

        void Resize(int widthPixels, int heightPixels, double rasterizationScale);
        [[nodiscard]] ContentRoot& Root();
        bool Focus(UIElement* element);
        void Shutdown();
        [[nodiscard]] bool IsActive() const noexcept { return m_active; }

    private:
        DesktopWindowHost();
        void VerifyAccess() const;
        static void ValidateSize(int widthPixels, int heightPixels, double scale);

        std::thread::id m_thread;
        std::shared_ptr<ContentRoot> m_root;
        std::shared_ptr<CompositionRenderer> m_renderer;
        bool m_active{ true };
    };
}
