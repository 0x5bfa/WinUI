#include <windows.h>
#include <UIAutomation.h>
#include <wrl/client.h>
#include <wrl/implements.h>
#include <algorithm>
#include <array>
#include <limits>

#pragma comment(lib, "Uiautomationcore.lib")

import std;
import winrt_base;
import Xaml.Runtime;

namespace
{
    using namespace xaml::runtime;
    using Microsoft::WRL::ComPtr;

    class AutomationProvider final : public Microsoft::WRL::RuntimeClass<
        Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>,
        IRawElementProviderFragmentRoot, IRawElementProviderSimple, IInvokeProvider>
    {
    public:
        AutomationProvider(HWND window, UIElement& element)
            : m_window(window), m_element(static_cast<DependencyObject&>(element).get_weak()) {}

        HRESULT STDMETHODCALLTYPE get_ProviderOptions(ProviderOptions* result) override
        {
            if (!result) { return E_POINTER; }
            *result = ProviderOptions_ServerSideProvider;
            return S_OK;
        }

        HRESULT STDMETHODCALLTYPE GetPatternProvider(PATTERNID id, IUnknown** result) override
        {
            if (!result) { return E_POINTER; }
            *result = nullptr;
            return Protected([&]
            {
                auto element = Element();
                if (id == UIA_InvokePatternId && element->AutomationControlRole() == AutomationRole::Button)
                {
                    return QueryInterface(IID_PPV_ARGS(result));
                }
                return S_OK;
            });
        }

        HRESULT STDMETHODCALLTYPE GetPropertyValue(PROPERTYID id, VARIANT* result) override
        {
            if (!result) { return E_POINTER; }
            VariantInit(result);
            return Protected([&]
            {
                auto element = Element();
                switch (id)
                {
                case UIA_NamePropertyId:
                    result->vt = VT_BSTR;
                    result->bstrVal = SysAllocString(element->AutomationLabel().c_str());
                    return result->bstrVal ? S_OK : E_OUTOFMEMORY;
                case UIA_ControlTypePropertyId:
                    result->vt = VT_I4;
                    switch (element->AutomationControlRole())
                    {
                    case AutomationRole::Button: result->lVal = UIA_ButtonControlTypeId; break;
                    case AutomationRole::Text: result->lVal = UIA_TextControlTypeId; break;
                    case AutomationRole::Image: result->lVal = UIA_ImageControlTypeId; break;
                    case AutomationRole::Group: result->lVal = UIA_GroupControlTypeId; break;
                    default: result->lVal = UIA_CustomControlTypeId; break;
                    }
                    return S_OK;
                case UIA_IsEnabledPropertyId: result->vt = VT_BOOL;
                    result->boolVal = element->IsEnabled() ? VARIANT_TRUE : VARIANT_FALSE; return S_OK;
                case UIA_IsKeyboardFocusablePropertyId: result->vt = VT_BOOL;
                    result->boolVal = element->IsKeyboardFocusable() ? VARIANT_TRUE : VARIANT_FALSE; return S_OK;
                case UIA_HasKeyboardFocusPropertyId: result->vt = VT_BOOL;
                    result->boolVal = element->HasFocus() ? VARIANT_TRUE : VARIANT_FALSE; return S_OK;
                case UIA_IsControlElementPropertyId:
                case UIA_IsContentElementPropertyId:
                    result->vt = VT_BOOL; result->boolVal = VARIANT_TRUE; return S_OK;
                default: return S_OK;
                }
            });
        }

        HRESULT STDMETHODCALLTYPE get_HostRawElementProvider(IRawElementProviderSimple** result) override
        {
            if (!result) { return E_POINTER; }
            *result = nullptr;
            return Protected([&]
            {
                if (Element()->Parent()) { return S_OK; }
                return UiaHostProviderFromHwnd(m_window, result);
            });
        }

        HRESULT STDMETHODCALLTYPE Navigate(NavigateDirection direction, IRawElementProviderFragment** result) override
        {
            if (!result) { return E_POINTER; }
            *result = nullptr;
            return Protected([&]
            {
                auto element = Element();
                winrt::com_ptr<UIElement> selected;
                auto parent = element->Parent();
                auto const count = element->VisualChildrenCount();
                switch (direction)
                {
                case NavigateDirection_Parent: selected = std::move(parent); break;
                case NavigateDirection_FirstChild: if (count) { selected = element->VisualChildAt(0); } break;
                case NavigateDirection_LastChild: if (count) { selected = element->VisualChildAt(count - 1); } break;
                case NavigateDirection_NextSibling:
                case NavigateDirection_PreviousSibling:
                    if (parent)
                    {
                        auto const siblings = parent->VisualChildrenCount();
                        for (std::size_t i = 0; i < siblings; ++i)
                        {
                            if (parent->VisualChildAt(i).get() != element) { continue; }
                            if (direction == NavigateDirection_NextSibling && i + 1 < siblings)
                            { selected = parent->VisualChildAt(i + 1); }
                            if (direction == NavigateDirection_PreviousSibling && i > 0)
                            { selected = parent->VisualChildAt(i - 1); }
                            break;
                        }
                    }
                    break;
                default: return E_INVALIDARG;
                }
                if (!selected) { return S_OK; }
                auto provider = Microsoft::WRL::Make<AutomationProvider>(m_window, *selected);
                return provider ? provider.CopyTo(result) : E_OUTOFMEMORY;
            });
        }

        HRESULT STDMETHODCALLTYPE GetRuntimeId(SAFEARRAY** result) override
        {
            if (!result) { return E_POINTER; }
            *result = nullptr;
            return Protected([&]
            {
                auto const id = Element()->RenderIdentity();
                auto array = SafeArrayCreateVector(VT_I4, 0, 3);
                if (!array) { return E_OUTOFMEMORY; }
                LONG indices[] = { 0, 1, 2 };
                LONG values[] = { UiaAppendRuntimeId, static_cast<LONG>(id), static_cast<LONG>(id >> 32) };
                for (int i = 0; i < 3; ++i)
                {
                    if (auto hr = SafeArrayPutElement(array, &indices[i], &values[i]); FAILED(hr))
                    { SafeArrayDestroy(array); return hr; }
                }
                *result = array;
                return S_OK;
            });
        }

        HRESULT STDMETHODCALLTYPE get_BoundingRectangle(UiaRect* result) override
        {
            if (!result) { return E_POINTER; }
            *result = {};
            return Protected([&]
            {
                auto element = Element();
                auto const size = element->ActualSize();
                std::array<Point, 4> corners{ Point{}, Point{ size.width, 0 },
                    Point{ 0, size.height }, Point{ size.width, size.height } };
                for (auto current = element; current;)
                {
                    auto const transform = current->TransformToParent();
                    for (auto& corner : corners) { corner = transform.Transform(corner); }
                    auto parent = current->Parent();
                    current = parent.get();
                }
                POINT origin{};
                if (!ClientToScreen(m_window, &origin)) { return HRESULT_FROM_WIN32(GetLastError()); }
                auto const scale = GetDpiForWindow(m_window) / 96.0;
                auto [minX, maxX] = std::minmax_element(corners.begin(), corners.end(),
                    [](Point a, Point b) { return a.x < b.x; });
                auto [minY, maxY] = std::minmax_element(corners.begin(), corners.end(),
                    [](Point a, Point b) { return a.y < b.y; });
                *result = { origin.x + minX->x * scale, origin.y + minY->y * scale,
                    (maxX->x - minX->x) * scale, (maxY->y - minY->y) * scale };
                return S_OK;
            });
        }

        HRESULT STDMETHODCALLTYPE GetEmbeddedFragmentRoots(SAFEARRAY** result) override
        { if (!result) { return E_POINTER; } *result = nullptr; return S_OK; }

        HRESULT STDMETHODCALLTYPE SetFocus() override
        { return Protected([&] { return Element()->Focus() ? S_OK : UIA_E_ELEMENTNOTENABLED; }); }

        HRESULT STDMETHODCALLTYPE get_FragmentRoot(IRawElementProviderFragmentRoot** result) override
        {
            if (!result) { return E_POINTER; }
            *result = nullptr;
            return Protected([&]
            {
                auto element = Element();
                while (auto parent = element->Parent()) { element = parent.get(); }
                auto provider = Microsoft::WRL::Make<AutomationProvider>(m_window, *element);
                return provider ? provider.CopyTo(result) : E_OUTOFMEMORY;
            });
        }

        HRESULT STDMETHODCALLTYPE ElementProviderFromPoint(double x, double y, IRawElementProviderFragment** result) override
        {
            if (!result) { return E_POINTER; }
            *result = nullptr;
            return Protected([&]
            {
                auto root = Element();
                while (auto parent = root->Parent()) { root = parent.get(); }
                POINT screen{ static_cast<LONG>(x), static_cast<LONG>(y) };
                if (!ScreenToClient(m_window, &screen)) { return HRESULT_FROM_WIN32(GetLastError()); }
                auto const scale = GetDpiForWindow(m_window) / 96.0;
                auto hit = root->HitTest({ screen.x / scale, screen.y / scale });
                if (!hit) { return S_OK; }
                auto provider = Microsoft::WRL::Make<AutomationProvider>(m_window, *hit);
                return provider ? provider.CopyTo(result) : E_OUTOFMEMORY;
            });
        }

        HRESULT STDMETHODCALLTYPE GetFocus(IRawElementProviderFragment** result) override
        {
            if (!result) { return E_POINTER; }
            *result = nullptr;
            return Protected([&]
            {
                auto element = Element();
                while (auto parent = element->Parent()) { element = parent.get(); }
                std::function<winrt::com_ptr<UIElement>(UIElement&)> find = [&](UIElement& node)
                    -> winrt::com_ptr<UIElement>
                {
                    if (node.HasFocus()) { return node.get_strong(); }
                    for (std::size_t i = 0; i < node.VisualChildrenCount(); ++i)
                    {
                        if (auto found = find(*node.VisualChildAt(i))) { return found; }
                    }
                    return nullptr;
                };
                auto focused = find(*element);
                if (!focused) { return S_OK; }
                auto provider = Microsoft::WRL::Make<AutomationProvider>(m_window, *focused);
                return provider ? provider.CopyTo(result) : E_OUTOFMEMORY;
            });
        }

        HRESULT STDMETHODCALLTYPE Invoke() override
        {
            return Protected([&]
            {
                auto element = Element();
                if (!element->IsEnabled()) { return UIA_E_ELEMENTNOTENABLED; }
                return element->AutomationInvoke() ? S_OK : UIA_E_NOTSUPPORTED;
            });
        }

    private:
        UIElement* Element() const
        {
            auto element = m_element.get();
            if (!element) { throw winrt::hresult(UIA_E_ELEMENTNOTAVAILABLE); }
            return static_cast<UIElement*>(element.get());
        }
        template <typename F> static HRESULT Protected(F&& body) noexcept
        {
            try { return body(); }
            catch (winrt::hresult_error const& error) { return error.code(); }
            catch (std::bad_alloc const&) { return E_OUTOFMEMORY; }
            catch (...) { return E_FAIL; }
        }
        HWND m_window{};
        winrt::weak_ref<DependencyObject> m_element;
    };
}

extern "C" std::intptr_t XamlGetAutomationObject(std::uintptr_t window,
    std::uintptr_t wparam, std::intptr_t lparam, xaml::runtime::UIElement* root)
{
    if (lparam != UiaRootObjectId || !root || !root->IsConnectedToRoot()) { return 0; }
    auto provider = Microsoft::WRL::Make<AutomationProvider>(reinterpret_cast<HWND>(window), *root);
    if (!provider) { throw winrt::hresult_out_of_memory(); }
    return UiaReturnRawElementProvider(reinterpret_cast<HWND>(window),
        static_cast<WPARAM>(wparam), static_cast<LPARAM>(lparam), provider.Get());
}
