#include <windows.h>
#include <winrt/Microsoft.UI.Interop.h>
#include <MddBootstrap.h>
#include <WindowsAppSDK-VersionInfo.h>

import std;
import winrt_base;
import winrt.Microsoft.UI.Dispatching;
import Xaml.Runtime;

using namespace xaml::runtime;

namespace
{
    struct Bootstrap
    {
        Bootstrap()
        {
            winrt::check_hresult(MddBootstrapInitialize2(WINDOWSAPPSDK_RELEASE_MAJORMINOR,
                WINDOWSAPPSDK_RELEASE_VERSION_TAG_W, PACKAGE_VERSION{ WINDOWSAPPSDK_RUNTIME_VERSION_UINT64 },
                MddBootstrapInitializeOptions_None));
        }
        ~Bootstrap() { MddBootstrapShutdown(); }
    };

    struct Apartment
    {
        Apartment() { winrt::init_apartment(winrt::apartment_type::single_threaded); }
        ~Apartment() { winrt::uninit_apartment(); }
    };

    struct Application
    {
        HWND window{};
        std::shared_ptr<DesktopWindowHost> host;
        winrt::com_ptr<FrameworkElement> element;

        void Resize()
        {
            if (!host || !host->IsActive()) { return; }
            RECT bounds{};
            winrt::check_bool(GetClientRect(window, &bounds));
            host->Resize(bounds.right, bounds.bottom, GetDpiForWindow(window) / 96.0);
        }

        void Initialize()
        {
            element = winrt::make_self<FrameworkElement>();
            element->Width(280);
            element->Height(160);
            element->Margin({ 32, 32, 32, 32 });
            element->HorizontalAlignmentState(HorizontalAlignment::Left);
            element->VerticalAlignmentState(VerticalAlignment::Top);
            (void)element->AddInputHandler(InputEvent::PointerPressed, [this](UIElement& sender, InputEventArgs& args)
            {
                if (!args.primaryButton) { return; }
                auto owner = host;
                if (owner->Focus(&sender) && owner->IsActive())
                {
                    args.handled = owner->Root().Input().CapturePointer(sender, args.pointerId);
                    sender.Opacity(0.55);
                }
            });
            (void)element->AddInputHandler(InputEvent::PointerReleased, [](UIElement& sender, InputEventArgs& args)
            {
                if (!args.primaryButton) { return; }
                sender.Opacity(1.0);
                args.handled = true;
            });
            (void)element->AddInputHandler(InputEvent::PointerCaptureLost, [](UIElement& sender, InputEventArgs&)
            {
                sender.Opacity(1.0);
            });
            (void)element->AddInputHandler(InputEvent::KeyDown, [this](UIElement& sender, InputEventArgs& args)
            {
                if (args.virtualKey == VK_SPACE && !args.repeatedKey)
                {
                    sender.Opacity(sender.Opacity() == 1.0 ? 0.55 : 1.0);
                    args.handled = true;
                }
                if (args.virtualKey == VK_ESCAPE)
                {
                    PostMessageW(window, WM_CLOSE, 0, 0);
                    args.handled = true;
                }
            });
            RECT bounds{};
            winrt::check_bool(GetClientRect(window, &bounds));
            auto const id = winrt::Microsoft::UI::GetWindowIdFromWindow(window);
            host = DesktopWindowHost::CreateForWindow(id.Value, element,
                bounds.right, bounds.bottom, GetDpiForWindow(window) / 96.0);
        }

        static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
        {
            auto* app = reinterpret_cast<Application*>(GetWindowLongPtrW(window, GWLP_USERDATA));
            if (message == WM_NCCREATE)
            {
                app = static_cast<Application*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
                app->window = window;
                SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
            }
            try
            {
                switch (message)
                {
                case WM_SIZE:
                    if (app) { app->Resize(); }
                    return 0;
                case WM_DPICHANGED:
                {
                    auto const* bounds = reinterpret_cast<RECT const*>(lparam);
                    SetWindowPos(window, nullptr, bounds->left, bounds->top,
                        bounds->right - bounds->left, bounds->bottom - bounds->top,
                        SWP_NOACTIVATE | SWP_NOZORDER);
                    if (app) { app->Resize(); }
                    return 0;
                }
                case WM_CLOSE:
                    if (app && app->host) { app->host->Shutdown(); }
                    DestroyWindow(window);
                    return 0;
                case WM_DESTROY:
                    if (app && app->host) { app->host->Shutdown(); }
                    PostQuitMessage(0);
                    return 0;
                case WM_NCDESTROY:
                    SetWindowLongPtrW(window, GWLP_USERDATA, 0);
                    if (app) { app->window = nullptr; }
                    break;
                }
            }
            catch (...)
            {
                OutputDebugStringW(winrt::to_message().c_str());
                PostMessageW(window, WM_CLOSE, 0, 0);
                return 0;
            }
            return DefWindowProcW(window, message, wparam, lparam);
        }

        ~Application()
        {
            if (host && host->IsActive()) { try { host->Shutdown(); } catch (...) {} }
            if (window) { DestroyWindow(window); }
        }
    };
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show)
{
    try
    {
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        Bootstrap bootstrap;
        Apartment apartment;
        auto queue = winrt::Microsoft::UI::Dispatching::DispatcherQueueController::CreateOnCurrentThread();
        int result{};
        try
        {
            Application app;
            WNDCLASSW windowClass{};
            windowClass.lpfnWndProc = Application::WindowProc;
            windowClass.hInstance = instance;
            windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
            windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
            windowClass.lpszClassName = L"XamlNativeDesktop";
            winrt::check_bool(RegisterClassW(&windowClass));
            winrt::check_bool(CreateWindowExW(0, windowClass.lpszClassName,
                L"Native runtime: click the rectangle, then press Space or Escape",
                WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 640, 420,
                nullptr, nullptr, instance, &app));
            app.Initialize();
            ShowWindow(app.window, show);
            MSG message{};
            for (;;)
            {
                auto const status = GetMessageW(&message, nullptr, 0, 0);
                if (status == -1) { winrt::throw_last_error(); }
                if (!status) { result = static_cast<int>(message.wParam); break; }
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
        }
        catch (...)
        {
            queue.ShutdownQueue();
            throw;
        }
        queue.ShutdownQueue();
        return result;
    }
    catch (...)
    {
        auto const message = winrt::to_message();
        MessageBoxW(nullptr, message.c_str(), L"Native runtime startup failed", MB_OK | MB_ICONERROR);
        return 1;
    }
}
