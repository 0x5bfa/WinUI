#pragma once

#include <cstdint>
#include <functional>
#include "DrawingTypes.h"

namespace xaml::runtime::platform
{
    enum class InputKind
    {
        Entered, Exited, Pressed, Moved, Released, CaptureLost, Wheel,
        KeyDown, KeyUp, CharacterReceived, FocusLost
    };

    // Platform facts only: no element identities or framework-owned references.
    struct InputPacket
    {
        InputKind kind{};
        float x{};
        float y{};
        std::uint32_t pointerId{};
        std::uint32_t virtualKey{};
        std::uint32_t character{};
        std::uint32_t modifiers{};
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
std::uintptr_t XamlRenderingInputWindow(void* backend);
void XamlRenderingContent(void* backend, std::uintptr_t element, std::uint64_t version,
    std::vector<xaml::drawing::Command> const& commands, bool rightToLeft, float width, float height);
bool XamlRenderingNeedsContent(void* backend, std::uintptr_t element, std::uint64_t version,
    bool rightToLeft, float width, float height);
bool XamlRenderingRecoverDevice(void* backend, std::int32_t error);
