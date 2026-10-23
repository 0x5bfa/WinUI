export module Xaml.Runtime:FrameworkElement;

import std;
import winrt_base;
import :Properties;
import :Primitives;

export namespace xaml::runtime
{
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
