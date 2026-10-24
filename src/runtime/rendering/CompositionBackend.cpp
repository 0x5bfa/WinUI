#include <winrt/Microsoft.UI.Content.h>
#include <winrt/Microsoft.UI.Dispatching.h>
#include <winrt/Microsoft.UI.Input.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.System.h>
#include <winrt/Windows.UI.h>
#include <winrt/Windows.UI.Composition.h>

#include "CompositionBackend.h"

#include <cstdint>
#include <cmath>
#include <thread>
#include <unordered_map>
#include <vector>
#include <memory>

namespace
{
    namespace composition = winrt::Windows::UI::Composition;
    namespace content = winrt::Microsoft::UI::Content;
    namespace input = winrt::Microsoft::UI::Input;
    namespace platform = xaml::runtime::platform;

    struct Node
    {
        composition::ContainerVisual container{ nullptr };
        composition::SpriteVisual fill{ nullptr };
        composition::InsetClip clip{ nullptr };
        std::uint64_t visited{};
        std::vector<std::uintptr_t> children;
        std::vector<std::uintptr_t> nextChildren;
    };

    struct Backend : std::enable_shared_from_this<Backend>
    {
        std::thread::id thread = std::this_thread::get_id();
        composition::Compositor compositor{ nullptr };
        composition::ContainerVisual root{ nullptr };
        composition::CompositionColorBrush brush{ nullptr };
        content::ContentIsland island{ nullptr };
        content::DesktopChildSiteBridge bridge{ nullptr };
        input::InputPointerSource pointer{ nullptr };
        input::InputKeyboardSource keyboard{ nullptr };
        input::InputFocusController focus{ nullptr };
        input::InputPointerSource::PointerEntered_revoker entered;
        input::InputPointerSource::PointerExited_revoker exited;
        input::InputPointerSource::PointerPressed_revoker pressed;
        input::InputPointerSource::PointerMoved_revoker moved;
        input::InputPointerSource::PointerReleased_revoker released;
        input::InputPointerSource::PointerCaptureLost_revoker captureLost;
        input::InputPointerSource::PointerRoutedAway_revoker routedAway;
        input::InputPointerSource::PointerRoutedReleased_revoker routedReleased;
        input::InputPointerSource::PointerWheelChanged_revoker wheel;
        input::InputKeyboardSource::KeyDown_revoker keyDown;
        input::InputKeyboardSource::KeyUp_revoker keyUp;
        input::InputKeyboardSource::SystemKeyDown_revoker systemKeyDown;
        input::InputKeyboardSource::SystemKeyUp_revoker systemKeyUp;
        input::InputFocusController::LostFocus_revoker focusLost;
        platform::InputCallback inputCallback;
        bool closed{};
        std::unordered_map<std::uintptr_t, Node> nodes;
        std::vector<std::uintptr_t> rootChildren;
        std::vector<std::uintptr_t> nextRootChildren;
        std::uint64_t generation{};
        float scale{ 1.0f };
        int widthPixels{};
        int heightPixels{};

        void Initialize(std::uint64_t parent, int width, int height)
        {
            auto queue = winrt::Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread();
            if (!queue)
            {
                throw winrt::hresult_illegal_method_call();
            }
            // The lifted queue owns shutdown of the system queue it ensures.
            // The host remains responsible for pumping and draining its queue.
            queue.EnsureSystemDispatcherQueue();
            compositor = composition::Compositor{};
            root = compositor.CreateContainerVisual();
            brush = compositor.CreateColorBrush({ 255, 72, 128, 211 });
            try
            {
                island = content::ContentIsland::CreateForSystemVisual(queue, root);
                bridge = content::DesktopChildSiteBridge::CreateWithDispatcherQueue(queue,
                    winrt::Microsoft::UI::WindowId{ parent });
                bridge.OverrideScale(scale);
                bridge.Connect(island);
                ConnectInput();
                Resize(width, height);
                bridge.Show();
            }
            catch (...)
            {
                Close();
                throw;
            }
        }

        void VerifyAccess() const
        {
            if (thread != std::this_thread::get_id()) { throw winrt::hresult_wrong_thread(); }
            if (closed) { throw winrt::hresult_illegal_method_call(); }
        }

        void ConnectInput()
        {
            pointer = input::InputPointerSource::GetForIsland(island);
            keyboard = input::InputKeyboardSource::GetForIsland(island);
            focus = input::InputFocusController::GetForIsland(island);
            if (!pointer || !keyboard || !focus) { throw winrt::hresult_illegal_method_call(); }
            auto weak = weak_from_this();
            auto pointerHandler = [weak](platform::InputKind kind)
            {
                return [weak, kind](auto const&, input::PointerEventArgs const& args)
                {
                    if (auto owner = weak.lock(); owner && !owner->closed)
                    {
                        auto point = args.CurrentPoint();
                        auto const position = point.Position();
                        auto properties = point.Properties();
                        auto const update = properties.PointerUpdateKind();
                        bool const mouse = point.PointerDeviceType() == input::PointerDeviceType::Mouse;
                        bool const primary = !mouse || (kind == platform::InputKind::Pressed ?
                            update == input::PointerUpdateKind::LeftButtonPressed :
                            kind == platform::InputKind::Released ?
                            update == input::PointerUpdateKind::LeftButtonReleased : properties.IsLeftButtonPressed());
                        platform::InputPacket packet{ .kind = kind, .x = position.X, .y = position.Y,
                            .pointerId = point.PointerId(), .timestamp = point.Timestamp(),
                            .wheelDelta = properties.MouseWheelDelta(),
                            .horizontalWheel = properties.IsHorizontalMouseWheel(),
                            .inContact = point.IsInContact(), .primaryButton = primary };
                        auto callback = owner->inputCallback;
                        if (callback && callback(packet)) { args.Handled(true); }
                    }
                };
            };
            entered = pointer.PointerEntered(winrt::auto_revoke, pointerHandler(platform::InputKind::Entered));
            exited = pointer.PointerExited(winrt::auto_revoke, pointerHandler(platform::InputKind::Exited));
            pressed = pointer.PointerPressed(winrt::auto_revoke, pointerHandler(platform::InputKind::Pressed));
            moved = pointer.PointerMoved(winrt::auto_revoke, pointerHandler(platform::InputKind::Moved));
            released = pointer.PointerReleased(winrt::auto_revoke, pointerHandler(platform::InputKind::Released));
            captureLost = pointer.PointerCaptureLost(winrt::auto_revoke, pointerHandler(platform::InputKind::CaptureLost));
            // Cross-island gesture continuation is not supported yet. Cancel
            // the element interaction when the platform routes contact away.
            routedAway = pointer.PointerRoutedAway(winrt::auto_revoke, pointerHandler(platform::InputKind::CaptureLost));
            routedReleased = pointer.PointerRoutedReleased(winrt::auto_revoke, pointerHandler(platform::InputKind::CaptureLost));
            wheel = pointer.PointerWheelChanged(winrt::auto_revoke, pointerHandler(platform::InputKind::Wheel));
            auto keyHandler = [weak](platform::InputKind kind, bool system)
            {
                return [weak, kind, system](auto const&, input::KeyEventArgs const& args)
                {
                    if (auto owner = weak.lock(); owner && !owner->closed)
                    {
                        platform::InputPacket packet{ .kind = kind,
                            .virtualKey = static_cast<std::uint32_t>(args.VirtualKey()),
                            .timestamp = args.Timestamp(),
                            .repeatedKey = kind == platform::InputKind::KeyDown && args.KeyStatus().WasKeyDown,
                            .systemKey = system };
                        auto callback = owner->inputCallback;
                        if (callback && callback(packet)) { args.Handled(true); }
                    }
                };
            };
            keyDown = keyboard.KeyDown(winrt::auto_revoke, keyHandler(platform::InputKind::KeyDown, false));
            keyUp = keyboard.KeyUp(winrt::auto_revoke, keyHandler(platform::InputKind::KeyUp, false));
            systemKeyDown = keyboard.SystemKeyDown(winrt::auto_revoke, keyHandler(platform::InputKind::KeyDown, true));
            systemKeyUp = keyboard.SystemKeyUp(winrt::auto_revoke, keyHandler(platform::InputKind::KeyUp, true));
            focusLost = focus.LostFocus(winrt::auto_revoke, [weak](auto const&, auto const&)
            {
                if (auto owner = weak.lock(); owner && !owner->closed)
                {
                    auto callback = owner->inputCallback;
                    if (callback) { callback(platform::InputPacket{ .kind = platform::InputKind::FocusLost }); }
                }
            });
        }

        void Resize(int width, int height)
        {
            VerifyAccess();
            widthPixels = width;
            heightPixels = height;
            bridge.MoveAndResize({ 0, 0, width, height });
            root.Size({ static_cast<float>(width) / scale, static_cast<float>(height) / scale });
        }

        void SetRasterizationScale(float nextScale)
        {
            VerifyAccess();
            scale = nextScale;
            // ContentIsland applies its scale once to the system visual.
            // PointerPoint.Position is already expressed in island DIPs.
            bridge.OverrideScale(scale);
            auto const visualScale = bridge.SiteView().ShouldApplyRasterizationScale() ? 1.0f : scale;
            root.Scale({ visualScale, visualScale, 1.0f });
            root.Size({ static_cast<float>(widthPixels) / scale,
                static_cast<float>(heightPixels) / scale });
        }

        void Begin()
        {
            VerifyAccess();
            ++generation;
            nextRootChildren.clear();
            for (auto& [_, node] : nodes) { node.nextChildren.clear(); }
        }

        void Realize(std::uintptr_t id, std::uintptr_t parent,
            float x, float y, float width, float height, float opacity, bool drawable, bool flipRTL,
            bool hasClip, float clipX, float clipY, float clipWidth, float clipHeight)
        {
            VerifyAccess();
            auto const [it, inserted] = nodes.try_emplace(id);
            auto& node = it->second;
            if (inserted)
            {
                node.container = compositor.CreateContainerVisual();
                node.fill = compositor.CreateSpriteVisual();
                node.fill.Brush(brush);
                node.container.Children().InsertAtBottom(node.fill);
            }
            node.visited = generation;
            // Mirror only at a direction boundary. The parent already
            // transforms descendants that inherit its flow direction.
            node.container.Scale({ flipRTL ? -1.0f : 1.0f, 1, 1 });
            node.container.Offset({ flipRTL ? x + width : x, y, 0 });
            node.container.Size({ width, height });
            node.container.Opacity(opacity);
            node.fill.Size({ width, height });
            node.fill.IsVisible(drawable);
            if (hasClip)
            {
                if (!node.clip)
                {
                    node.clip = compositor.CreateInsetClip();
                    node.container.Clip(node.clip);
                }
                node.clip.LeftInset(clipX);
                node.clip.TopInset(clipY);
                node.clip.RightInset(width - (clipX + clipWidth));
                node.clip.BottomInset(height - (clipY + clipHeight));
            }
            else if (node.clip)
            {
                node.container.Clip(nullptr);
                node.clip = nullptr;
            }
            if (parent) { nodes.at(parent).nextChildren.push_back(id); }
            else { nextRootChildren.push_back(id); }
        }

        void DetachChangedChildren(composition::ContainerVisual const& container,
            std::vector<std::uintptr_t> const& existing, std::vector<std::uintptr_t> const& next)
        {
            if (existing == next) { return; }
            container.Children().RemoveAll();
        }

        void AttachChangedChildren(composition::ContainerVisual const& container,
            composition::SpriteVisual const& fill,
            std::vector<std::uintptr_t>& existing, std::vector<std::uintptr_t>& next)
        {
            if (existing == next) { return; }
            if (fill) { container.Children().InsertAtBottom(fill); }
            for (auto id : next) { container.Children().InsertAtTop(nodes.at(id).container); }
            existing.swap(next);
        }

        void End()
        {
            VerifyAccess();
            // Detach every changed collection before reparenting its visuals.
            // This allows moving an element between live containers.
            for (auto& [_, node] : nodes)
            {
                if (node.visited != generation)
                {
                    // A live child may move out of a removed parent during
                    // this frame. Release that old visual parent first.
                    node.container.Children().RemoveAll();
                }
                else
                {
                    DetachChangedChildren(node.container, node.children, node.nextChildren);
                }
            }
            DetachChangedChildren(root, rootChildren, nextRootChildren);
            for (auto& [_, node] : nodes)
            {
                if (node.visited == generation)
                {
                    AttachChangedChildren(node.container, node.fill, node.children, node.nextChildren);
                }
            }
            AttachChangedChildren(root, nullptr, rootChildren, nextRootChildren);
            for (auto it = nodes.begin(); it != nodes.end();)
            {
                if (it->second.visited != generation) { it = nodes.erase(it); }
                else { ++it; }
            }
        }

        void Close() noexcept
        {
            if (closed) { return; }
            closed = true;
            inputCallback = {};
            entered.revoke(); exited.revoke(); pressed.revoke(); moved.revoke();
            released.revoke(); captureLost.revoke(); wheel.revoke();
            routedAway.revoke(); routedReleased.revoke();
            keyDown.revoke(); keyUp.revoke(); systemKeyDown.revoke(); systemKeyUp.revoke();
            focusLost.revoke();
            pointer = nullptr; keyboard = nullptr; focus = nullptr;
            nodes.clear();
            try { if (bridge) { bridge.Close(); } } catch (...) {}
            try { if (island) { island.Close(); } } catch (...) {}
            bridge = nullptr; island = nullptr;
            brush = nullptr; root = nullptr; compositor = nullptr;
        }

        ~Backend() { Close(); }
    };

    using BackendHandle = std::shared_ptr<Backend>;
    BackendHandle GetBackend(void* handle) { return *static_cast<BackendHandle*>(handle); }
}

extern "C" void* XamlRenderingCreate(std::uint64_t window, int width, int height)
{
    auto backend = std::make_shared<Backend>();
    backend->Initialize(window, width, height);
    return new BackendHandle(std::move(backend));
}
extern "C" void XamlRenderingResize(void* backend, int width, int height)
{
    GetBackend(backend)->Resize(width, height);
}
extern "C" void XamlRenderingSetRasterizationScale(void* backend, float scale)
{
    GetBackend(backend)->SetRasterizationScale(scale);
}
extern "C" void XamlRenderingBegin(void* backend)
{
    GetBackend(backend)->Begin();
}
extern "C" void XamlRenderingNode(void* backend, std::uintptr_t element, std::uintptr_t parent,
    float x, float y, float width, float height, float opacity, bool drawable, bool flipRTL,
    bool hasClip, float clipX, float clipY, float clipWidth, float clipHeight)
{
    GetBackend(backend)->Realize(element, parent, x, y, width, height, opacity, drawable, flipRTL,
        hasClip, clipX, clipY, clipWidth, clipHeight);
}
extern "C" void XamlRenderingEnd(void* backend)
{
    GetBackend(backend)->End();
}
extern "C" void XamlRenderingDestroy(void* backend)
{
    std::unique_ptr<BackendHandle> handle(static_cast<BackendHandle*>(backend));
    (*handle)->Close();
}

void XamlRenderingSetInput(void* backend, platform::InputCallback callback)
{
    auto owner = GetBackend(backend);
    owner->VerifyAccess();
    owner->inputCallback = std::move(callback);
}

bool XamlRenderingFocusInput(void* backend)
{
    auto owner = GetBackend(backend);
    owner->VerifyAccess();
    return owner->focus.TrySetFocus();
}
