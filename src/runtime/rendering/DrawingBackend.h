#pragma once

#include "DrawingTypes.h"
#include <memory>
#include <winrt/Windows.UI.Composition.h>

namespace xaml::runtime::platform
{
    class DrawingBackend final
    {
    public:
        explicit DrawingBackend(winrt::Windows::UI::Composition::Compositor const& compositor);
        ~DrawingBackend();
        void CheckDevice();
        void ResetDevice();
        [[nodiscard]] std::uint64_t Generation() const noexcept;
        void Draw(winrt::Windows::UI::Composition::ContainerVisual const& target,
            std::vector<drawing::Command> const& commands, float scale, bool rightToLeft, float width);
    private:
        struct Impl;
        std::unique_ptr<Impl> m_impl;
    };
}
