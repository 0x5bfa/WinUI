export module Xaml.Runtime:FrameworkElement;

import std;
import winrt_base;
import :Properties;
import :Primitives;
import :Styling;

export namespace xaml::runtime
{
    enum class FlowDirection
    {
        LeftToRight,
        RightToLeft
    };

    struct FrameworkElement : winrt::implements<FrameworkElement, UIElement>
    {
        ~FrameworkElement();
        static DependencyProperty<winrt::Windows::Foundation::IInspectable> const DataContextProperty;
        static DependencyProperty<ElementTheme> const RequestedThemeProperty;
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
        [[nodiscard]] winrt::Windows::Foundation::IInspectable DataContext() const;
        void DataContext(winrt::Windows::Foundation::IInspectable const& value);
        [[nodiscard]] ElementTheme RequestedTheme() const;
        void RequestedTheme(ElementTheme theme);
        [[nodiscard]] ElementTheme ActualTheme() const;
        [[nodiscard]] std::shared_ptr<ResourceDictionary> Resources();
        [[nodiscard]] std::optional<std::any> TryFindResource(std::wstring_view key) const;
        void SetResourceReference(DependencyPropertyHandle property, std::wstring key, bool templateValue = false);
        void StyleValue(std::shared_ptr<Style> style);
        [[nodiscard]] std::shared_ptr<Style> StyleValue() const;
        void SetNameScope(std::shared_ptr<NameScope> scope);
        [[nodiscard]] winrt::com_ptr<DependencyObject> FindName(std::wstring_view name) const;
        void TemplatedParent(FrameworkElement* parent);
        [[nodiscard]] winrt::com_ptr<FrameworkElement> TemplatedParent() const;

    protected:
        void OnPropertyChanged(PropertyChange const& change) override;
        void OnVisualAncestryChanged() override;
        void OnVisualParentChanged() override;
        Size MeasureCore(Size available) override;
        Size ArrangeCore(Size finalSize) override;
        void OnActualSizeChanged(Size previous, Size current) override;
        virtual Size MeasureOverride(Size available);
        virtual Size ArrangeOverride(Size finalSize);
        virtual void OnResourcesChanged() {}
        virtual void OnStyleChanged() {}
        void WatchResources();
        void RefreshResources();
    private:
        std::shared_ptr<ResourceDictionary> m_resources;
        std::shared_ptr<Style> m_style;
        std::vector<DependencyPropertyHandle> m_styleProperties;
        std::uint64_t m_styleVersion{};
        std::shared_ptr<NameScope> m_nameScope;
        winrt::weak_ref<DependencyObject> m_templatedParent;
        struct ResourceReference
        {
            DependencyPropertyHandle property;
            std::wstring key;
            std::uint64_t token;
            bool templateValue;
        };
        std::vector<ResourceReference> m_resourceReferences;
        std::uint64_t m_resourceSubscription{};
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
