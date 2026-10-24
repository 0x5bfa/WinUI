module Xaml.Runtime;

import std;
import winrt_base;

namespace xaml::runtime
{
    DesktopWindowHost::DesktopWindowHost() : m_thread(std::this_thread::get_id()) {}

    void DesktopWindowHost::ValidateSize(int widthPixels, int heightPixels, double scale)
    {
        if (widthPixels < 0 || heightPixels < 0 || !std::isfinite(scale) ||
            scale < std::numeric_limits<float>::epsilon() || scale > std::numeric_limits<float>::max())
        {
            throw winrt::hresult_invalid_argument();
        }
    }

    void DesktopWindowHost::VerifyAccess() const
    {
        if (m_thread != std::this_thread::get_id()) { throw winrt::hresult_wrong_thread(); }
        if (!m_active) { throw winrt::hresult_illegal_method_call(); }
    }

    std::shared_ptr<DesktopWindowHost> DesktopWindowHost::CreateForWindow(std::uint64_t windowId,
        winrt::com_ptr<UIElement> element, int widthPixels, int heightPixels, double rasterizationScale)
    {
        ValidateSize(widthPixels, heightPixels, rasterizationScale);
        if (!element) { throw winrt::hresult_invalid_argument(); }
        element->VerifyAccess();
        auto host = std::shared_ptr<DesktopWindowHost>(new DesktopWindowHost());
        host->m_renderer = CompositionRenderer::CreateForWindow(windowId, widthPixels, heightPixels);
        host->m_renderer->SetRasterizationScale(rasterizationScale);
        host->m_textInput = std::make_shared<TextInputSession>(host->m_renderer->InputWindow());
        host->m_textInput->NotifyLayoutChanged(rasterizationScale);
        host->m_root = ContentRoot::CreateOnCurrentDispatcher(std::move(element),
            { widthPixels / rasterizationScale, heightPixels / rasterizationScale },
            [renderer = host->m_renderer, text = std::weak_ptr<TextInputSession>(host->m_textInput)](UIElement& root)
            {
                renderer->Render(root);
                if (renderer->IsActive())
                { if (auto input = text.lock()) { input->NotifyLayoutChanged(renderer->RasterizationScale()); } }
            });
        host->m_root->SetRasterizationScale(rasterizationScale);
        std::weak_ptr<TextInputSession> weakText = host->m_textInput;
        host->m_root->Input().SetFocusObserver([weakText](UIElement* focused)
        {
            if (auto text = weakText.lock()) { text->Focus(focused); }
        });
        std::weak_ptr<CompositionRenderer> weakRenderer = host->m_renderer;
        host->m_root->Input().SetFocusRequest([weakRenderer]
        {
            auto renderer = weakRenderer.lock();
            return renderer && renderer->IsActive() && renderer->FocusInput();
        });
        std::weak_ptr<ContentRoot> weak = host->m_root;
        host->m_renderer->SetInputHandler([weak](InputEventArgs args)
        {
            auto root = weak.lock();
            if (!root || !root->IsActive()) { return false; }
            // Hit testing consumes settled layout, including changes made
            // since the last queued frame.
            if (args.event != InputEvent::HostFocusLost && args.event != InputEvent::KeyDown &&
                args.event != InputEvent::KeyUp && args.event != InputEvent::CharacterReceived) { root->UpdateLayout(); }
            return root->IsActive() && root->Input().Process(args);
        });
        return host;
    }

    DesktopWindowHost::~DesktopWindowHost()
    {
        if (m_active && m_thread == std::this_thread::get_id())
        {
            try { Shutdown(); } catch (...) {}
        }
    }

    void DesktopWindowHost::Resize(int widthPixels, int heightPixels, double rasterizationScale)
    {
        VerifyAccess();
        ValidateSize(widthPixels, heightPixels, rasterizationScale);
        // Keep the root alive if a synchronous callback releases the host.
        auto root = m_root;
        auto renderer = m_renderer;
        renderer->SetRasterizationScale(rasterizationScale);
        renderer->Resize(widthPixels, heightPixels);
        if (!root->IsActive()) { return; }
        root->SetRasterizationScale(rasterizationScale);
        root->Resize({ widthPixels / rasterizationScale, heightPixels / rasterizationScale });
        if (m_textInput) { m_textInput->NotifyLayoutChanged(rasterizationScale); }
    }

    ContentRoot& DesktopWindowHost::Root()
    {
        VerifyAccess();
        return *m_root;
    }

    bool DesktopWindowHost::Focus(UIElement* element)
    {
        VerifyAccess();
        auto root = m_root;
        auto renderer = m_renderer;
        if (element && !renderer->FocusInput()) { return false; }
        return root->IsActive() && root->Input().Focus(element);
    }
    TextInputSession& DesktopWindowHost::TextInput() { VerifyAccess(); return *m_textInput; }
    bool DesktopWindowHost::PreTranslateMessage(std::uint32_t message, std::uintptr_t wparam, std::intptr_t lparam)
    {
        VerifyAccess(); auto text = m_textInput; return text->PreTranslateMessage(message, wparam, lparam);
    }

    void DesktopWindowHost::Shutdown()
    {
        if (m_thread != std::this_thread::get_id()) { throw winrt::hresult_wrong_thread(); }
        if (!m_active) { return; }
        m_active = false;
        auto root = std::exchange(m_root, {});
        auto renderer = std::exchange(m_renderer, {});
        auto text = std::exchange(m_textInput, {});
        std::exception_ptr failure;
        if (renderer && renderer->IsActive()) { renderer->SetInputHandler({}); }
        try { if (root) { root->Shutdown(); } }
        catch (...) { failure = std::current_exception(); }
        try { if (text) { text->Shutdown(); } }
        catch (...) { if (!failure) { failure = std::current_exception(); } }
        if (renderer && renderer->IsActive()) { renderer->Shutdown(); }
        if (failure) { std::rethrow_exception(failure); }
    }
}
