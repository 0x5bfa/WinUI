export module Xaml.Controls;

import std;
import winrt_base;
export import Xaml.Runtime;

export namespace xaml::controls
{
    struct Control : winrt::implements<Control, runtime::FrameworkElement>
    {
        static runtime::DependencyProperty<runtime::Thickness> const PaddingProperty;

        [[nodiscard]] runtime::Thickness Padding() const;
        void Padding(runtime::Thickness value);
        [[nodiscard]] bool IsFocused() const;
        void SetFocused(bool focused);

    protected:
        void OnIsEnabledChanged() override;
        void OnVisualParentChanged() override;
        void OnVisualAncestryChanged() override;
        virtual void OnFocusLost() {}

    private:
        bool m_focused{};
    };

    struct ContentControl : winrt::implements<ContentControl, Control>
    {
        [[nodiscard]] winrt::com_ptr<runtime::UIElement> Content() const;
        void Content(runtime::UIElement* content);

    protected:
        runtime::Size MeasureOverride(runtime::Size available) override;
        runtime::Size ArrangeOverride(runtime::Size finalSize) override;
    };

    struct Panel : winrt::implements<Panel, runtime::FrameworkElement>
    {
        [[nodiscard]] std::size_t ChildrenCount() const;
        [[nodiscard]] winrt::com_ptr<runtime::UIElement> ChildAt(std::size_t index) const;
        void Append(runtime::UIElement* child);
        void Insert(std::size_t index, runtime::UIElement* child);
        void RemoveAt(std::size_t index);
        void Move(std::size_t from, std::size_t to);
        void Clear();

    protected:
        [[nodiscard]] std::vector<winrt::com_ptr<runtime::UIElement>> SnapshotChildren() const;
    };

    enum class Orientation { Vertical, Horizontal };

    struct StackPanel : winrt::implements<StackPanel, Panel>
    {
        static runtime::DependencyProperty<controls::Orientation> const OrientationProperty;
        static runtime::DependencyProperty<double> const SpacingProperty;

        [[nodiscard]] controls::Orientation Orientation() const;
        void Orientation(controls::Orientation value);
        [[nodiscard]] double Spacing() const;
        void Spacing(double value);

    protected:
        runtime::Size MeasureOverride(runtime::Size available) override;
        runtime::Size ArrangeOverride(runtime::Size finalSize) override;
    };

    enum class ClickMode { Release, Press, Hover };
    enum class ActivationKey { Space, Enter };
    enum class PointerAction { Entered, Exited, Pressed, Moved, Released, Cancelled, CaptureLost };

    // Coordinates/hit testing and physical capture belong to the input backend.
    // These are already-targeted, element-relative input facts.
    struct PointerInput
    {
        PointerAction action;
        std::uint32_t id;
        bool isOver;
        bool isPrimaryButton{ true };
    };

    struct ButtonBase : winrt::implements<ButtonBase, ContentControl>
    {
        using ClickHandler = std::function<void(ButtonBase&)>;
        using EventToken = std::uint64_t;

        static runtime::DependencyProperty<controls::ClickMode> const ClickModeProperty;
        [[nodiscard]] controls::ClickMode ClickMode() const;
        void ClickMode(controls::ClickMode value);
        [[nodiscard]] bool IsPressed() const;
        [[nodiscard]] bool IsPointerOver() const;
        [[nodiscard]] std::optional<std::uint32_t> ActivePointer() const;
        [[nodiscard]] EventToken AddClick(ClickHandler handler);
        void RemoveClick(EventToken token);
        bool ProcessPointer(PointerInput const& input);
        bool ProcessKey(ActivationKey key, bool isDown);
        void CancelInteraction();

    protected:
        void OnIsEnabledChanged() override;
        void OnVisualParentChanged() override;
        void OnVisualAncestryChanged() override;
        void OnFocusLost() override;
        void OnPropertyChanged(runtime::PropertyChange const& change) override;
        virtual void OnClick();

    private:
        struct Subscription
        {
            EventToken token;
            ClickHandler handler;
            bool active{ true };
        };
        std::vector<std::shared_ptr<Subscription>> m_click;
        std::optional<std::uint32_t> m_pointer;
        std::optional<ActivationKey> m_key;
        EventToken m_nextToken{};
        bool m_pointerOver{};
    };

    enum class ButtonVisualState { Normal, PointerOver, Pressed, Disabled };

    struct Button : winrt::implements<Button, ButtonBase>
    {
        [[nodiscard]] ButtonVisualState VisualState() const;
    };
}
