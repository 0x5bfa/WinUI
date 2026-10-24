export module Xaml.Runtime:Primitives;

import std;
import winrt_base;
import :Properties;
import :DependencyObject;

export namespace xaml::runtime
{
    struct Size
    {
        double width{};
        double height{};
        bool operator==(Size const&) const = default;
    };

    struct Point
    {
        double x{};
        double y{};
        bool operator==(Point const&) const = default;
    };

    struct Rect
    {
        double x{};
        double y{};
        double width{};
        double height{};
        bool operator==(Rect const&) const = default;
    };

    struct Thickness
    {
        double left{};
        double top{};
        double right{};
        double bottom{};
        bool operator==(Thickness const&) const = default;
    };

    enum class Visibility
    {
        Visible,
        Collapsed
    };

    enum class HorizontalAlignment
    {
        Left,
        Center,
        Right,
        Stretch
    };

    enum class VerticalAlignment
    {
        Top,
        Center,
        Bottom,
        Stretch
    };

    struct UIElement;

    enum class InputEvent
    {
        PointerEntered, PointerExited, PointerPressed, PointerMoved, PointerReleased,
        PointerCanceled, PointerCaptureLost, PointerWheelChanged,
        KeyDown, KeyUp, GotFocus, LostFocus, HostFocusLost
    };

    // Native input contract. Positions are root-relative DIPs. The original
    // source is borrowed for the duration of synchronous event delivery.
    struct InputEventArgs
    {
        InputEvent event{};
        UIElement* originalSource{};
        Point position{};
        std::uint32_t pointerId{};
        std::uint32_t virtualKey{};
        std::uint64_t timestamp{};
        int wheelDelta{};
        bool horizontalWheel{};
        bool inContact{};
        bool primaryButton{};
        bool repeatedKey{};
        bool systemKey{};
        bool handled{};
    };

    struct UIElement : winrt::implements<UIElement, DependencyObject>
    {
        friend class ContentRoot;
        friend class InputManager;
        static DependencyProperty<bool> const IsEnabledProperty;
        static DependencyProperty<bool> const IsHitTestVisibleProperty;
        static DependencyProperty<Visibility> const VisibilityProperty;
        static DependencyProperty<double> const OpacityProperty;
        static DependencyProperty<bool> const UseLayoutRoundingProperty;

        [[nodiscard]] bool IsEnabled() const;
        void IsEnabled(bool value);
        [[nodiscard]] bool IsHitTestVisible() const;
        void IsHitTestVisible(bool value);
        [[nodiscard]] Visibility VisibilityState() const;
        void VisibilityState(Visibility value);
        [[nodiscard]] double Opacity() const;
        void Opacity(double value);
        [[nodiscard]] bool UseLayoutRounding() const;
        void UseLayoutRounding(bool value);
        // Defaults to the visual parent's effective direction. FrameworkElement
        // supplies the dependency property and can override this value.
        [[nodiscard]] virtual bool IsRightToLeft() const;
        // Native rectangle bridge until the WinRT RectangleGeometry contract is implemented.
        [[nodiscard]] std::optional<Rect> ClipBounds() const;
        void ClipBounds(std::optional<Rect> value);
        [[nodiscard]] winrt::com_ptr<UIElement> Parent() const;
        [[nodiscard]] std::size_t VisualChildrenCount() const;
        [[nodiscard]] winrt::com_ptr<UIElement> VisualChildAt(std::size_t index) const;

        void Measure(Size available);
        void Arrange(Rect slot);
        void UpdateLayout();
        [[nodiscard]] Size DesiredSize() const;
        [[nodiscard]] Size ActualSize() const;
        [[nodiscard]] Rect LayoutSlot() const;
        [[nodiscard]] Rect LayoutBounds() const;
        // The input backend supplies parent-local coordinates in DIPs.
        [[nodiscard]] winrt::com_ptr<UIElement> HitTest(Point position) const;
        [[nodiscard]] Point PointFromRoot(Point position) const;
        using InputHandler = std::function<void(UIElement&, InputEventArgs&)>;
        [[nodiscard]] std::int64_t AddInputHandler(InputEvent event, InputHandler handler,
            bool handledEventsToo = false);
        void RemoveInputHandler(std::int64_t token);
        [[nodiscard]] bool IsMeasureValid() const;
        [[nodiscard]] bool IsArrangeValid() const;
        [[nodiscard]] std::uint64_t RenderVersion() const;
        [[nodiscard]] bool IsConnectedToRoot() const;
        [[nodiscard]] bool IsLoaded() const;
        using LifecycleHandler = std::function<void(UIElement&)>;
        [[nodiscard]] std::int64_t AddLoadingHandler(LifecycleHandler handler);
        [[nodiscard]] std::int64_t AddLoadedHandler(LifecycleHandler handler);
        [[nodiscard]] std::int64_t AddUnloadedHandler(LifecycleHandler handler);
        void RemoveLoadingHandler(std::int64_t token);
        void RemoveLoadedHandler(std::int64_t token);
        void RemoveUnloadedHandler(std::int64_t token);
        void InvalidateMeasure();
        void InvalidateArrange();
        void InvalidateRender();

    protected:
        void SetSingleVisualChild(UIElement* child);
        void ReplaceVisualChildren(std::span<UIElement* const> children);
        void OnPropertyChanged(PropertyChange const& change) override;
        virtual void OnIsEnabledChanged() {}
        virtual void OnVisualParentChanged() {}
        virtual void OnVisualAncestryChanged() {}
        virtual void OnLoading() {}
        virtual void OnLoaded() {}
        virtual void OnUnloaded() {}
        virtual void OnInputEvent(InputEventArgs&) {}
        virtual void OnActualSizeChanged(Size, Size) {}
        virtual Size MeasureCore(Size available);
        virtual Size ArrangeCore(Size finalSize);
        void SetLayoutOffset(double x, double y);
        void InvalidateInheritedFlowDirection();

    private:
        using DeferredEnqueue = std::function<bool(std::function<void()>)>;
        void SetRootInvalidationCallback(std::function<void()> callback, DeferredEnqueue deferred = {},
            std::function<void()> updateLayout = {});
        void RequestRootUpdate();
        void NotifyIsEnabledChanged();
        void NotifyVisualAncestryChanged();
        void NotifyDescendantsOfVisualAncestryChange();
        void UpdateRootConnection(bool connected, DeferredEnqueue const& deferred = {});
        void DispatchLoaded();
        void SetLayoutScale(double scale);
        void MarkLayoutScaleSubtree(double scale);
        void MarkLayoutRoundingSubtree();
        void MarkFlowDirectionSubtree();
        enum class LifecycleEvent { Loading, Loaded, Unloaded };
        struct LifecycleSubscription
        {
            LifecycleEvent event;
            std::int64_t token;
            LifecycleHandler handler;
            bool active{ true };
        };
        [[nodiscard]] std::int64_t AddLifecycleHandler(LifecycleEvent event, LifecycleHandler handler);
        void RemoveLifecycleHandler(LifecycleEvent event, std::int64_t token);
        void RaiseLifecycleEvent(LifecycleEvent event);
        void RaiseInputEvent(InputEventArgs& args);
        struct InputSubscription
        {
            std::int64_t token;
            InputEvent event;
            InputHandler handler;
            bool handledEventsToo{};
            bool active{ true };
        };

        winrt::weak_ref<DependencyObject> m_parent;
        std::vector<winrt::com_ptr<UIElement>> m_children;
        Size m_available{};
        Size m_desired{};
        Size m_actual{};
        Rect m_slot{};
        double m_offsetX{};
        double m_offsetY{};
        double m_layoutScale{ 1.0 };
        std::optional<Rect> m_clipBounds;
        std::uint64_t m_measureVersion{};
        std::uint64_t m_arrangeVersion{};
        bool m_measureDirty{ true };
        bool m_arrangeDirty{ true };
        std::uint64_t m_renderVersion{ 1 };
        bool m_measuring{};
        bool m_arranging{};
        bool m_connected{};
        bool m_loadingRaised{};
        bool m_loaded{};
        std::int64_t m_nextLifecycleToken{};
        std::vector<std::shared_ptr<LifecycleSubscription>> m_lifecycleSubscriptions;
        std::function<void()> m_rootInvalidated;
        std::function<void()> m_rootUpdateLayout;
        DeferredEnqueue m_deferredEnqueue;
        std::int64_t m_nextInputToken{};
        std::vector<std::shared_ptr<InputSubscription>> m_inputSubscriptions;
    };

}
