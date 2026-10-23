export module Xaml.Runtime:Primitives;

import std;
import winrt_base;
import :ObjectModel;
import :Properties;

export namespace xaml::runtime
{
    struct Size
    {
        double width{};
        double height{};
        bool operator==(Size const&) const = default;
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

    struct UIElement : winrt::implements<UIElement, DependencyObject>
    {
        static DependencyProperty<bool> const IsEnabledProperty;

        [[nodiscard]] bool IsEnabled() const;
        void IsEnabled(bool value);
        [[nodiscard]] winrt::com_ptr<UIElement> Parent() const;
        [[nodiscard]] std::size_t VisualChildrenCount() const;
        [[nodiscard]] winrt::com_ptr<UIElement> VisualChildAt(std::size_t index) const;

        void Measure(Size available);
        void Arrange(Rect slot);
        [[nodiscard]] Size DesiredSize() const;
        [[nodiscard]] Size ActualSize() const;
        [[nodiscard]] Rect LayoutSlot() const;
        [[nodiscard]] bool IsMeasureValid() const;
        [[nodiscard]] bool IsArrangeValid() const;
        [[nodiscard]] std::uint64_t RenderVersion() const;
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
        virtual Size MeasureCore(Size available);
        virtual Size ArrangeCore(Size finalSize);

    private:
        void NotifyIsEnabledChanged();
        void NotifyVisualAncestryChanged();
        void NotifyDescendantsOfVisualAncestryChange();

        winrt::weak_ref<Object> m_parent;
        std::vector<winrt::com_ptr<UIElement>> m_children;
        Size m_available{};
        Size m_desired{};
        Size m_actual{};
        Rect m_slot{};
        std::uint64_t m_measureVersion{};
        std::uint64_t m_arrangeVersion{};
        bool m_measureDirty{ true };
        bool m_arrangeDirty{ true };
        std::uint64_t m_renderVersion{ 1 };
        bool m_measuring{};
        bool m_arranging{};
    };

    struct FrameworkElement : winrt::implements<FrameworkElement, UIElement>
    {
        static DependencyProperty<double> const WidthProperty;
        static DependencyProperty<double> const HeightProperty;

        [[nodiscard]] double Width() const;
        void Width(double value);
        [[nodiscard]] double Height() const;
        void Height(double value);

    protected:
        Size MeasureCore(Size available) override;
        Size ArrangeCore(Size finalSize) override;
        virtual Size MeasureOverride(Size available);
        virtual Size ArrangeOverride(Size finalSize);
    };
}
