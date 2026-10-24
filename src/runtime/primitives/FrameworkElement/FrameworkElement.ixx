export module Xaml.Runtime:FrameworkElement;

import std;
import winrt_base;
import :Properties;
import :Primitives;

export namespace xaml::runtime
{
    enum class FlowDirection
    {
        LeftToRight,
        RightToLeft
    };

    struct FrameworkElement : winrt::implements<FrameworkElement, UIElement>
    {
        static DependencyProperty<double> const WidthProperty;
        static DependencyProperty<double> const HeightProperty;
        static DependencyProperty<double> const MinWidthProperty;
        static DependencyProperty<double> const MaxWidthProperty;
        static DependencyProperty<double> const MinHeightProperty;
        static DependencyProperty<double> const MaxHeightProperty;
        static DependencyProperty<Thickness> const MarginProperty;
        static DependencyProperty<HorizontalAlignment> const HorizontalAlignmentProperty;
        static DependencyProperty<VerticalAlignment> const VerticalAlignmentProperty;
        static DependencyProperty<FlowDirection> const FlowDirectionProperty;

        [[nodiscard]] double Width() const;
        void Width(double value);
        [[nodiscard]] double Height() const;
        void Height(double value);
        [[nodiscard]] double MinWidth() const;
        void MinWidth(double value);
        [[nodiscard]] double MaxWidth() const;
        void MaxWidth(double value);
        [[nodiscard]] double MinHeight() const;
        void MinHeight(double value);
        [[nodiscard]] double MaxHeight() const;
        void MaxHeight(double value);
        [[nodiscard]] Thickness Margin() const;
        void Margin(Thickness value);
        [[nodiscard]] HorizontalAlignment HorizontalAlignmentState() const;
        void HorizontalAlignmentState(HorizontalAlignment value);
        [[nodiscard]] VerticalAlignment VerticalAlignmentState() const;
        void VerticalAlignmentState(VerticalAlignment value);
        [[nodiscard]] FlowDirection FlowDirectionState() const;
        void FlowDirectionState(FlowDirection value);
        [[nodiscard]] bool IsRightToLeft() const override;
        using SizeChangedHandler = std::function<void(FrameworkElement&, Size, Size)>;
        [[nodiscard]] std::int64_t AddSizeChangedHandler(SizeChangedHandler handler);
        void RemoveSizeChangedHandler(std::int64_t token);

    protected:
        void OnPropertyChanged(PropertyChange const& change) override;
        Size MeasureCore(Size available) override;
        Size ArrangeCore(Size finalSize) override;
        void OnActualSizeChanged(Size previous, Size current) override;
        virtual Size MeasureOverride(Size available);
        virtual Size ArrangeOverride(Size finalSize);
    private:
        struct SizeSubscription
        {
            std::int64_t token;
            SizeChangedHandler handler;
            bool active{ true };
        };
        std::vector<std::shared_ptr<SizeSubscription>> m_sizeHandlers;
        std::int64_t m_nextSizeToken{};
    };
}
