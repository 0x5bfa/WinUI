#include <windows.h>
#include <winrt/Microsoft.UI.Interop.h>
#include <MddBootstrap.h>
#include <WindowsAppSDK-VersionInfo.h>

import std;
import winrt_base;
import winrt.Microsoft.UI.Dispatching;
import Xaml.Runtime;
import Xaml.Controls;

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
            using namespace xaml::controls;
            auto panel = winrt::make_self<StackPanel>();
            panel->Margin({ 32, 32, 32, 32 });
            panel->Spacing(16);
            panel->HorizontalAlignmentState(HorizontalAlignment::Left);
            panel->VerticalAlignmentState(VerticalAlignment::Top);
            auto status = winrt::make_self<TextBlock>();
            status->Text(L"WUC rendering / lifted input / native control templates");
            panel->Append(status.get());
            auto label = winrt::make_self<TextBlock>();
            label->Text(L"Click, Space, or Enter");
            auto button = winrt::make_self<Button>();
            button->Content(label.get());
            button->TabIndex(0);
            auto weakStatus = static_cast<DependencyObject&>(*status).get_weak();
            (void)button->AddClick([weakStatus, count = 0u](ButtonBase&) mutable
            {
                if (auto value = weakStatus.get())
                {
                    dynamic_cast<TextBlock&>(*value).Text(winrt::hstring(L"Activations: " + std::to_wstring(++count)));
                }
            });
            panel->Append(button.get());
            auto secondLabel = winrt::make_self<TextBlock>();
            secondLabel->Text(L"Tab / Shift+Tab moves focus");
            auto second = winrt::make_self<Button>();
            second->Content(secondLabel.get());
            second->TabIndex(1);
            panel->Append(second.get());
            element = panel;
            (void)element->AddInputHandler(InputEvent::KeyDown, [this](UIElement&, InputEventArgs& args)
            {
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
                L"Unified native controls: templates, text, input, and focus",
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
                if (app.host && app.host->IsActive() &&
                    app.host->PreTranslateMessage(message.message, message.wParam, message.lParam)) { continue; }
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
