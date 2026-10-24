export module Xaml.Runtime:Rendering;

import std;
import winrt_base;
import :Primitives;

export namespace xaml::runtime
{
    // The native tree is projected into one desktop Composition island.
    // The backend retains Composition visuals but no UIElement references.
    class CompositionRenderer final
    {
    public:
        // parentWindowId is Microsoft.UI.WindowId.Value, not an HWND.
        static std::shared_ptr<CompositionRenderer> CreateForWindow(
            std::uint64_t parentWindowId, int widthPixels, int heightPixels);
        ~CompositionRenderer();

        CompositionRenderer(CompositionRenderer const&) = delete;
        CompositionRenderer& operator=(CompositionRenderer const&) = delete;

        void Render(UIElement& root);
        void Resize(int widthPixels, int heightPixels);
        void SetRasterizationScale(double scale);
        [[nodiscard]] double RasterizationScale() const;
        void Shutdown();
        using InputHandler = std::function<bool(InputEventArgs)>;
        void SetInputHandler(InputHandler handler);
        bool FocusInput();
        [[nodiscard]] bool IsActive() const noexcept { return m_backend != nullptr; }

    private:
        explicit CompositionRenderer(void* backend);
        void Realize(UIElement& element, std::uintptr_t parent);
        void VerifyAccess() const;

        std::thread::id m_thread;
        void* m_backend{};
        double m_scale{ 1.0 };
    };
}
