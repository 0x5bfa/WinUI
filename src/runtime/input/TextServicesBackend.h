#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

namespace xaml::runtime::platform
{
    struct TextSelection { std::uint32_t start{}, end{}; bool activeAtStart{}; };
    struct TextBounds { double x{}, y{}, width{}, height{}; };
    struct TextCallbacks
    {
        std::function<std::wstring()> text;
        std::function<TextSelection()> selection;
        std::function<void(TextSelection)> select;
        std::function<void(std::uint32_t, std::uint32_t, std::wstring_view)> replace;
        std::function<TextBounds(std::uint32_t, std::uint32_t)> bounds;
        std::function<std::uint32_t(double, double)> position;
        std::function<bool()> readOnly;
        std::function<void(std::uint32_t, std::uint32_t, bool)> composition;
    };
}
void* XamlTextServicesCreate(std::uintptr_t window);
void XamlTextServicesFocus(void* session, xaml::runtime::platform::TextCallbacks callbacks);
void XamlTextServicesChanged(void* session, std::uint32_t start, std::uint32_t oldEnd, std::uint32_t newEnd);
void XamlTextServicesSelectionChanged(void* session);
void XamlTextServicesLayoutChanged(void* session, double scale);
bool XamlTextServicesKey(void* session, std::uint32_t message, std::uintptr_t wparam, std::intptr_t lparam);
void XamlTextServicesDestroy(void* session);
