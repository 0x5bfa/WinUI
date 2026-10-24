#pragma once

#include <cstdint>
#include <functional>

namespace xaml::runtime::platform
{
    enum class InputKind
    {
        Entered, Exited, Pressed, Moved, Released, CaptureLost, Wheel,
        KeyDown, KeyUp, FocusLost
    };

    // Platform facts only: no element identities or framework-owned references.
    struct InputPacket
    {
        InputKind kind{};
        float x{};
        float y{};
        std::uint32_t pointerId{};
        std::uint32_t virtualKey{};
        std::uint64_t timestamp{};
        int wheelDelta{};
        bool horizontalWheel{};
        bool inContact{};
        bool primaryButton{};
        bool repeatedKey{};
        bool systemKey{};
    };

    using InputCallback = std::function<bool(InputPacket const&)>;
}

void XamlRenderingSetInput(void* backend, xaml::runtime::platform::InputCallback callback);
bool XamlRenderingFocusInput(void* backend);
