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
    struct SmokePanel : winrt::implements<SmokePanel, FrameworkElement>
    {
        int loading{};
        int loaded{};
        int unloaded{};

        void SetChild(UIElement* child) { SetSingleVisualChild(child); }
        void SetChildren(UIElement* first, UIElement* second)
        {
            UIElement* children[] = { first, second };
            ReplaceVisualChildren(children);
        }

    protected:
        void OnLoading() override { ++loading; }
        void OnLoaded() override { ++loaded; }
        void OnUnloaded() override { ++unloaded; }

        Size MeasureOverride(Size available) override
        {
            Size result{};
            auto const count = VisualChildrenCount();
            for (std::size_t i = 0; i < count; ++i)
            {
                auto child = VisualChildAt(i);
                child->Measure({ available.width / static_cast<double>(count), available.height });
                auto const desired = child->DesiredSize();
                result.width += desired.width;
                result.height = std::max(result.height, desired.height);
            }
            return result;
        }

        Size ArrangeOverride(Size finalSize) override
        {
            auto const count = VisualChildrenCount();
            if (!count) { return finalSize; }
            auto const width = finalSize.width / static_cast<double>(count);
            for (std::size_t i = 0; i < count; ++i)
            {
                VisualChildAt(i)->Arrange({ width * static_cast<double>(i), 0, width, finalSize.height });
            }
            return finalSize;
        }
    };

    void PumpMessages(DWORD milliseconds)
    {
        auto const end = GetTickCount64() + milliseconds;
        while (GetTickCount64() < end)
        {
            MSG message{};
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
            {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
            Sleep(10);
        }
    }
}

int main()
{
    char const* stage = "bootstrap";
    bool bootstrapped = false;
    try
    {
        winrt::check_hresult(MddBootstrapInitialize2(WINDOWSAPPSDK_RELEASE_MAJORMINOR,
            WINDOWSAPPSDK_RELEASE_VERSION_TAG_W, PACKAGE_VERSION{ WINDOWSAPPSDK_RUNTIME_VERSION_UINT64 },
            MddBootstrapInitializeOptions_None));
        bootstrapped = true;
        stage = "apartment";
        winrt::init_apartment(winrt::apartment_type::single_threaded);
        stage = "dispatcher queue";
        auto queue = winrt::Microsoft::UI::Dispatching::DispatcherQueueController::CreateOnCurrentThread();

        WNDCLASSW cls{};
        cls.lpfnWndProc = DefWindowProcW;
        cls.hInstance = GetModuleHandleW(nullptr);
        cls.lpszClassName = L"XamlRenderingSmoke";
        if (!RegisterClassW(&cls)) { throw std::runtime_error("Could not register a window class."); }
        HWND window = CreateWindowExW(0, cls.lpszClassName, L"XAML rendering smoke",
            WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT, 480, 320,
            nullptr, nullptr, cls.hInstance, nullptr);
        if (!window) { throw std::runtime_error("Could not create an HWND."); }

        stage = "composition backend";
        RECT client{};
        GetClientRect(window, &client);
        auto id = winrt::Microsoft::UI::GetWindowIdFromWindow(window);
        auto renderer = CompositionRenderer::CreateForWindow(id.Value,
            client.right - client.left, client.bottom - client.top);
        stage = "content root";
        auto element = winrt::make_self<SmokePanel>();
        auto left = winrt::make_self<SmokePanel>();
        auto right = winrt::make_self<SmokePanel>();
        auto leaf = winrt::make_self<FrameworkElement>();
        leaf->Width(80);
        leaf->Height(60);
        left->SetChild(leaf.get());
        element->SetChildren(left.get(), right.get());
        winrt::com_ptr<UIElement> visual;
        visual.copy_from(element.get());
        auto root = ContentRoot::CreateOnCurrentDispatcher(visual,
            { static_cast<double>(client.right), static_cast<double>(client.bottom) },
            [renderer](UIElement& item) { renderer->Render(item); });

        stage = "message pump";
        PumpMessages(900);

        if (!root->FrameCount() || !element->IsLoaded() || !leaf->IsLoaded() ||
            element->loading != 1 || element->loaded != 1)
        {
            throw std::runtime_error("Initial layout/render lifecycle failed.");
        }
        stage = "visual reparenting";
        auto const firstFrameCount = root->FrameCount();
        left->SetChild(nullptr);
        right->SetChild(leaf.get());
        PumpMessages(250);
        if (root->FrameCount() <= firstFrameCount || !leaf->IsLoaded() ||
            leaf->Parent().get() != right.get())
        {
            throw std::runtime_error("Reparent did not produce a settled rendering frame.");
        }

        stage = "resize";
        MoveWindow(window, 0, 0, 640, 360, TRUE);
        GetClientRect(window, &client);
        renderer->Resize(client.right, client.bottom);
        auto const secondFrameCount = root->FrameCount();
        root->Resize({ static_cast<double>(client.right), static_cast<double>(client.bottom) });
        PumpMessages(250);
        if (root->FrameCount() <= secondFrameCount)
        {
            throw std::runtime_error("Resize did not render a new frame.");
        }

        stage = "composition DPI scaling";
        double constexpr dpiScale = 1.25;
        auto const beforeDpiFrame = root->FrameCount();
        renderer->SetRasterizationScale(dpiScale);
        root->SetRasterizationScale(dpiScale);
        root->Resize({ client.right / dpiScale, client.bottom / dpiScale });
        PumpMessages(200);
        if (root->FrameCount() <= beforeDpiFrame ||
            renderer->RasterizationScale() != dpiScale ||
            root->RasterizationScale() != dpiScale ||
            element->LayoutSlot().width != client.right / dpiScale)
        {
            throw std::runtime_error("DIP layout and composition root scale diverged.");
        }
        renderer->SetRasterizationScale(1);
        root->SetRasterizationScale(1);
        root->Resize({ static_cast<double>(client.right), static_cast<double>(client.bottom) });
        PumpMessages(180);

        stage = "composition opacity";
        auto const thirdFrameCount = root->FrameCount();
        right->Opacity(0.35);
        PumpMessages(200);
        if (root->FrameCount() <= thirdFrameCount || !right->IsArrangeValid())
        {
            throw std::runtime_error("Changing composition opacity failed to render a new frame.");
        }

        stage = "composition rectangle clipping";
        auto const clipFrameCount = root->FrameCount();
        leaf->ClipBounds(Rect{ 2, 3, 30, 20 });
        PumpMessages(180);
        if (root->FrameCount() <= clipFrameCount || !leaf->ClipBounds())
        {
            throw std::runtime_error("Adding a composition clip did not produce a settled frame.");
        }
        auto const unclippedFrameCount = root->FrameCount();
        leaf->ClipBounds(std::nullopt);
        PumpMessages(180);
        if (root->FrameCount() <= unclippedFrameCount || leaf->ClipBounds())
        {
            throw std::runtime_error("Removing a composition clip did not produce a settled frame.");
        }

        stage = "composition flow direction";
        auto const flowFrameCount = root->FrameCount();
        element->FlowDirectionState(FlowDirection::RightToLeft);
        leaf->FlowDirectionState(FlowDirection::LeftToRight);
        PumpMessages(180);
        if (root->FrameCount() <= flowFrameCount ||
            !element->IsRightToLeft() || !right->IsRightToLeft() || leaf->IsRightToLeft())
        {
            throw std::runtime_error("Inherited and overridden RTL did not reach Composition.");
        }
        element->ClearValue(FrameworkElement::FlowDirectionProperty);
        leaf->ClearValue(FrameworkElement::FlowDirectionProperty);
        PumpMessages(180);

        stage = "desktop child site";
        int children{};
        EnumChildWindows(window, [](HWND, LPARAM context) -> BOOL
        {
            ++*reinterpret_cast<int*>(context);
            return TRUE;
        }, reinterpret_cast<LPARAM>(&children));
        if (!children) { throw std::runtime_error("Composition did not create a desktop child site."); }

        stage = "shutdown";
        root->Shutdown();
        PumpMessages(150);
        if (element->IsLoaded() || leaf->IsLoaded() ||
            element->unloaded != 1 || right->unloaded != 1)
        {
            throw std::runtime_error("Shutdown did not detach loaded visuals.");
        }
        root = nullptr;
        renderer->Shutdown();
        renderer = nullptr;
        DestroyWindow(window);
        queue.ShutdownQueue();
        winrt::uninit_apartment();
        MddBootstrapShutdown();
        std::cout << "Desktop Composition island smoke passed.\n";
        return 0;
    }
    catch (winrt::hresult_error const& error)
    {
        std::cerr << "At " << stage << ": WinRT 0x" << std::hex << static_cast<std::uint32_t>(error.code())
            << ": " << winrt::to_string(error.message()) << '\n';
    }
    catch (std::exception const& error) { std::cerr << "At " << stage << ": " << error.what() << '\n'; }
    if (bootstrapped) { MddBootstrapShutdown(); }
    return 1;
}
