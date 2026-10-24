module;
#include "CompositionBackend.h"

module Xaml.Runtime;

import std;
import winrt_base;

extern "C"
{
    void* XamlRenderingCreate(std::uint64_t window, int width, int height);
    void XamlRenderingResize(void* backend, int width, int height);
    void XamlRenderingSetRasterizationScale(void* backend, float scale);
    void XamlRenderingBegin(void* backend);
    void XamlRenderingNode(void* backend, std::uintptr_t element, std::uintptr_t parent,
        float x, float y, float width, float height, float opacity, bool drawable, bool flipRTL,
        bool hasClip, float clipX, float clipY, float clipWidth, float clipHeight);
    void XamlRenderingEnd(void* backend);
    void XamlRenderingDestroy(void* backend);
}

namespace xaml::runtime
{
    std::shared_ptr<CompositionRenderer> CompositionRenderer::CreateForWindow(
        std::uint64_t parentWindowId, int widthPixels, int heightPixels)
    {
        if (!parentWindowId || widthPixels < 0 || heightPixels < 0)
        {
            throw winrt::hresult_invalid_argument();
        }
        return std::shared_ptr<CompositionRenderer>(
            new CompositionRenderer(XamlRenderingCreate(parentWindowId, widthPixels, heightPixels)));
    }

    CompositionRenderer::CompositionRenderer(void* backend)
        : m_thread(std::this_thread::get_id()), m_backend(backend) {}

    CompositionRenderer::~CompositionRenderer()
    {
        if (m_backend && m_thread == std::this_thread::get_id()) { Shutdown(); }
    }

    void CompositionRenderer::VerifyAccess() const
    {
        if (m_thread != std::this_thread::get_id()) { throw winrt::hresult_wrong_thread(); }
        if (!m_backend) { throw winrt::hresult_illegal_method_call(); }
    }

    void CompositionRenderer::Resize(int widthPixels, int heightPixels)
    {
        VerifyAccess();
        if (widthPixels < 0 || heightPixels < 0) { throw winrt::hresult_invalid_argument(); }
        XamlRenderingResize(m_backend, widthPixels, heightPixels);
    }

    void CompositionRenderer::SetRasterizationScale(double scale)
    {
        VerifyAccess();
        if (!std::isfinite(scale) || scale < std::numeric_limits<float>::epsilon() ||
            scale > std::numeric_limits<float>::max())
        {
            throw winrt::hresult_invalid_argument();
        }
        if (m_scale == scale) { return; }
        XamlRenderingSetRasterizationScale(m_backend, static_cast<float>(scale));
        m_scale = scale;
    }

    double CompositionRenderer::RasterizationScale() const
    {
        VerifyAccess();
        return m_scale;
    }

    void CompositionRenderer::Realize(UIElement& element, std::uintptr_t parent)
    {
        if (element.VisibilityState() == Visibility::Collapsed) { return; }
        auto const bounds = element.LayoutBounds();
        auto const id = reinterpret_cast<std::uintptr_t>(&element);
        auto const clip = element.ClipBounds();
        auto const ancestor = element.Parent();
        bool const parentRTL = ancestor && ancestor->IsRightToLeft();
        XamlRenderingNode(m_backend, id, parent, static_cast<float>(bounds.x),
            static_cast<float>(bounds.y), static_cast<float>(bounds.width),
            static_cast<float>(bounds.height), static_cast<float>(element.Opacity()),
            element.VisualChildrenCount() == 0, element.IsRightToLeft() != parentRTL, clip.has_value(),
            static_cast<float>(clip ? clip->x : 0), static_cast<float>(clip ? clip->y : 0),
            static_cast<float>(clip ? clip->width : 0), static_cast<float>(clip ? clip->height : 0));
        for (std::size_t index = 0; index < element.VisualChildrenCount(); ++index)
        {
            auto child = element.VisualChildAt(index);
            Realize(*child, id);
        }
    }

    void CompositionRenderer::Render(UIElement& root)
    {
        VerifyAccess();
        if (!root.IsArrangeValid() || !root.IsConnectedToRoot())
        {
            throw winrt::hresult_illegal_method_call();
        }
        XamlRenderingBegin(m_backend);
        Realize(root, 0);
        XamlRenderingEnd(m_backend);
    }

    void CompositionRenderer::Shutdown()
    {
        VerifyAccess();
        auto* backend = std::exchange(m_backend, nullptr);
        XamlRenderingDestroy(backend);
    }

    void CompositionRenderer::SetInputHandler(InputHandler handler)
    {
        VerifyAccess();
        if (!handler) { XamlRenderingSetInput(m_backend, {}); return; }
        XamlRenderingSetInput(m_backend, [handler = std::move(handler)](platform::InputPacket const& packet)
        {
            InputEvent event{};
            switch (packet.kind)
            {
            case platform::InputKind::Entered: event = InputEvent::PointerEntered; break;
            case platform::InputKind::Exited: event = InputEvent::PointerExited; break;
            case platform::InputKind::Pressed: event = InputEvent::PointerPressed; break;
            case platform::InputKind::Moved: event = InputEvent::PointerMoved; break;
            case platform::InputKind::Released: event = InputEvent::PointerReleased; break;
            case platform::InputKind::CaptureLost: event = InputEvent::PointerCaptureLost; break;
            case platform::InputKind::Wheel: event = InputEvent::PointerWheelChanged; break;
            case platform::InputKind::KeyDown: event = InputEvent::KeyDown; break;
            case platform::InputKind::KeyUp: event = InputEvent::KeyUp; break;
            case platform::InputKind::FocusLost: event = InputEvent::HostFocusLost; break;
            }
            return handler(InputEventArgs{ .event = event,
                .position = { packet.x, packet.y }, .pointerId = packet.pointerId,
                .virtualKey = packet.virtualKey, .timestamp = packet.timestamp,
                .wheelDelta = packet.wheelDelta, .horizontalWheel = packet.horizontalWheel,
                .inContact = packet.inContact,
                .primaryButton = packet.primaryButton, .repeatedKey = packet.repeatedKey,
                .systemKey = packet.systemKey });
        });
    }

    bool CompositionRenderer::FocusInput()
    {
        VerifyAccess();
        return XamlRenderingFocusInput(m_backend);
    }
}
