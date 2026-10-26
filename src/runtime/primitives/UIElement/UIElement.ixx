export module Xaml.Runtime:Primitives;

import std;
import winrt_base;
import :Properties;
import :DependencyObject;
import :Drawing;

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

    // Row-vector affine transform in DIPs. Composition and hit testing use
    // the same local-to-parent matrix and inverse.
    struct Matrix3x2
    {
        double m11{ 1 }, m12{}, m21{}, m22{ 1 }, m31{}, m32{};
        bool operator==(Matrix3x2 const&) const = default;

        [[nodiscard]] static constexpr Matrix3x2 Translation(double x, double y) noexcept
        {
            return { 1, 0, 0, 1, x, y };
        }
        [[nodiscard]] static constexpr Matrix3x2 Scale(double x, double y) noexcept
        {
            return { x, 0, 0, y, 0, 0 };
        }
        [[nodiscard]] static constexpr Matrix3x2 Multiply(Matrix3x2 a, Matrix3x2 b) noexcept
        {
            return { a.m11 * b.m11 + a.m12 * b.m21, a.m11 * b.m12 + a.m12 * b.m22,
                a.m21 * b.m11 + a.m22 * b.m21, a.m21 * b.m12 + a.m22 * b.m22,
                a.m31 * b.m11 + a.m32 * b.m21 + b.m31,
                a.m31 * b.m12 + a.m32 * b.m22 + b.m32 };
        }
        [[nodiscard]] constexpr Point Transform(Point p) const noexcept
        {
            return { p.x * m11 + p.y * m21 + m31, p.x * m12 + p.y * m22 + m32 };
        }
        [[nodiscard]] std::optional<Matrix3x2> TryInvert() const noexcept
        {
            auto const determinant = m11 * m22 - m12 * m21;
            if (!std::isfinite(determinant) || determinant == 0) { return std::nullopt; }
            Matrix3x2 inverse{ m22 / determinant, -m12 / determinant,
                -m21 / determinant, m11 / determinant,
                (m21 * m32 - m31 * m22) / determinant,
                (m31 * m12 - m11 * m32) / determinant };
            if (!std::isfinite(inverse.m11) || !std::isfinite(inverse.m12) ||
                !std::isfinite(inverse.m21) || !std::isfinite(inverse.m22) ||
                !std::isfinite(inverse.m31) || !std::isfinite(inverse.m32)) { return std::nullopt; }
            return inverse;
        }
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
    struct TextSelection
    {
        std::uint32_t start{}, end{};
        bool activeAtStart{};
    };
    // Optional contract on the element itself; UTF-16 offsets, root-DIP geometry.
    struct TextInputClient
    {
        virtual ~TextInputClient() = default;
        virtual std::wstring TextContent() const = 0;
        virtual TextSelection Selection() const = 0;
        virtual void Selection(TextSelection value) = 0;
        virtual void ReplaceText(std::uint32_t start, std::uint32_t end, std::wstring_view text) = 0;
        virtual Rect TextBounds(std::uint32_t start, std::uint32_t end) const = 0;
        virtual std::uint32_t TextPosition(Point rootPosition) const = 0;
        virtual bool IsReadOnly() const { return false; }
        virtual void CompositionChanged(std::uint32_t start, std::uint32_t end, bool active) = 0;
    };
    class InputManager;
    enum class FocusNavigationDirection { Next, Previous, First, Last };
    enum class KeyboardNavigationMode { Local, Cycle, Once };
    enum class AutomationRole { Custom, Group, Button, Text, Image };

    enum class InputEvent
    {
        PointerEntered, PointerExited, PointerPressed, PointerMoved, PointerReleased,
        PointerCanceled, PointerCaptureLost, PointerWheelChanged,
        KeyDown, KeyUp, CharacterReceived, GotFocus, LostFocus, HostFocusLost
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
        std::uint32_t character{};
        std::uint32_t modifiers{}; // Shift=1, Control=2, Alt=4, Windows=8.
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
        UIElement();
        friend class ContentRoot;
        friend class InputManager;
        static DependencyProperty<bool> const IsEnabledProperty;
        static DependencyProperty<bool> const IsHitTestVisibleProperty;
        static DependencyProperty<Visibility> const VisibilityProperty;
        static DependencyProperty<double> const OpacityProperty;
        static DependencyProperty<Matrix3x2> const RenderTransformProperty;
        static DependencyProperty<Point> const RenderTransformOriginProperty;
        static DependencyProperty<winrt::hstring> const AutomationNameProperty;
        static DependencyProperty<bool> const UseLayoutRoundingProperty;
        static DependencyProperty<KeyboardNavigationMode> const TabFocusNavigationProperty;

        [[nodiscard]] bool IsEnabled() const;
        void IsEnabled(bool value);
        [[nodiscard]] bool IsHitTestVisible() const;
        void IsHitTestVisible(bool value);
        [[nodiscard]] Visibility VisibilityState() const;
        void VisibilityState(Visibility value);
        [[nodiscard]] double Opacity() const;
        void Opacity(double value);
        [[nodiscard]] Matrix3x2 RenderTransform() const;
        void RenderTransform(Matrix3x2 value);
        [[nodiscard]] Point RenderTransformOrigin() const;
        void RenderTransformOrigin(Point value);
        [[nodiscard]] Matrix3x2 TransformToParent() const;
        [[nodiscard]] winrt::hstring AutomationName() const;
        void AutomationName(winrt::hstring value);
        [[nodiscard]] virtual AutomationRole AutomationControlRole() const { return AutomationRole::Custom; }
        [[nodiscard]] virtual winrt::hstring AutomationLabel() const { return AutomationName(); }
        virtual bool AutomationInvoke() { return false; }
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
        [[nodiscard]] std::shared_ptr<InputManager> InputContext() const;
        bool Focus();
        bool CapturePointer(std::uint32_t pointerId);
        void ReleasePointerCapture(std::uint32_t pointerId);
        [[nodiscard]] bool HasFocus() const;
        [[nodiscard]] virtual bool IsKeyboardFocusable() const { return false; }
        [[nodiscard]] virtual int KeyboardTabIndex() const { return 0; }
        [[nodiscard]] bool IsMeasureValid() const;
        [[nodiscard]] bool IsArrangeValid() const;
        [[nodiscard]] std::uint64_t RenderVersion() const;
        [[nodiscard]] std::uint64_t RenderIdentity() const noexcept { return m_renderIdentity; }
        [[nodiscard]] DrawingContext CollectDrawingContent() const;
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
        std::optional<std::any> GetInheritedValue(DependencyPropertyHandle const& property) const override;
        void OnInheritedPropertyChanged(DependencyPropertyHandle const& property,
            std::any const& before, std::any const& after) override;
        virtual void OnIsEnabledChanged() {}
        virtual bool IsEnabledCore() const { return true; }
        void InvalidateIsEnabled();
        virtual void OnVisualParentChanged() {}
        virtual void OnVisualAncestryChanged() {}
        virtual void OnLoading() {}
        virtual void OnLoaded() {}
        virtual void OnUnloaded() {}
        virtual void OnInputEvent(InputEventArgs&) {}
        virtual void OnActualSizeChanged(Size, Size) {}
        virtual void OnRender(DrawingContext&) const {}
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
        std::uint64_t const m_renderIdentity;
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
        std::weak_ptr<InputManager> m_rootInput;
        winrt::weak_ref<DependencyObject> m_lastTabFocus;
    };

}
