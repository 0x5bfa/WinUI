module Xaml.Runtime;

import std;
import winrt_base;

namespace xaml::runtime
{
    namespace
    {
        bool ValidLength(double value) noexcept
        {
            return std::isnan(value) || (std::isfinite(value) && value >= 0);
        }

        bool ValidMinimum(double value) noexcept
        {
            return std::isfinite(value) && value >= 0;
        }

        bool ValidMaximum(double value) noexcept
        {
            return !std::isnan(value) && value >= 0;
        }

        bool ValidMargin(Thickness value) noexcept
        {
            return std::isfinite(value.left) && std::isfinite(value.top) &&
                std::isfinite(value.right) && std::isfinite(value.bottom) &&
                std::isfinite(value.left + value.right) && std::isfinite(value.top + value.bottom);
        }

        bool ValidHorizontalAlignment(HorizontalAlignment value) noexcept
        {
            return value == HorizontalAlignment::Left || value == HorizontalAlignment::Center ||
                value == HorizontalAlignment::Right || value == HorizontalAlignment::Stretch;
        }

        bool ValidVerticalAlignment(VerticalAlignment value) noexcept
        {
            return value == VerticalAlignment::Top || value == VerticalAlignment::Center ||
                value == VerticalAlignment::Bottom || value == VerticalAlignment::Stretch;
        }

        bool ValidFlowDirection(FlowDirection value) noexcept
        {
            return value == FlowDirection::LeftToRight || value == FlowDirection::RightToLeft;
        }

        double EffectiveLength(double preferred, double minimum, double maximum) noexcept
        {
            return std::max(minimum, std::min(preferred, maximum));
        }

        double ConstrainedSpace(double available, double margin) noexcept
        {
            return std::max(0.0, available - margin);
        }

        void ValidateSize(Size size, bool allowInfinity)
        {
            auto valid = [allowInfinity](double value)
            {
                return value >= 0 && (allowInfinity || std::isfinite(value));
            };
            if (!valid(size.width) || !valid(size.height))
            {
                throw winrt::hresult_invalid_argument();
            }
        }

    }

    DependencyProperty<double> const FrameworkElement::WidthProperty =
        DependencyProperty<double>::Register<FrameworkElement>("Width", std::numeric_limits<double>::quiet_NaN(),
            PropertyEffects::Measure | PropertyEffects::Render, ValidLength);

    DependencyProperty<double> const FrameworkElement::HeightProperty =
        DependencyProperty<double>::Register<FrameworkElement>("Height", std::numeric_limits<double>::quiet_NaN(),
            PropertyEffects::Measure | PropertyEffects::Render, ValidLength);

    DependencyProperty<double> const FrameworkElement::MinWidthProperty =
        DependencyProperty<double>::Register<FrameworkElement>("MinWidth", 0.0, PropertyEffects::Measure, ValidMinimum);

    DependencyProperty<double> const FrameworkElement::MaxWidthProperty =
        DependencyProperty<double>::Register<FrameworkElement>("MaxWidth", std::numeric_limits<double>::infinity(),
            PropertyEffects::Measure, ValidMaximum);

    DependencyProperty<double> const FrameworkElement::MinHeightProperty =
        DependencyProperty<double>::Register<FrameworkElement>("MinHeight", 0.0, PropertyEffects::Measure, ValidMinimum);

    DependencyProperty<double> const FrameworkElement::MaxHeightProperty =
        DependencyProperty<double>::Register<FrameworkElement>("MaxHeight", std::numeric_limits<double>::infinity(),
            PropertyEffects::Measure, ValidMaximum);

    DependencyProperty<Thickness> const FrameworkElement::MarginProperty =
        DependencyProperty<Thickness>::Register<FrameworkElement>("Margin", {}, PropertyEffects::Measure, ValidMargin);

    DependencyProperty<HorizontalAlignment> const FrameworkElement::HorizontalAlignmentProperty =
        DependencyProperty<HorizontalAlignment>::Register<FrameworkElement>("HorizontalAlignment",
            HorizontalAlignment::Stretch, PropertyEffects::Arrange, ValidHorizontalAlignment);

    DependencyProperty<VerticalAlignment> const FrameworkElement::VerticalAlignmentProperty =
        DependencyProperty<VerticalAlignment>::Register<FrameworkElement>("VerticalAlignment",
            VerticalAlignment::Stretch, PropertyEffects::Arrange, ValidVerticalAlignment);

    DependencyProperty<FlowDirection> const FrameworkElement::FlowDirectionProperty =
        DependencyProperty<FlowDirection>::Register<FrameworkElement>("FlowDirection",
            FlowDirection::LeftToRight, PropertyEffects::Arrange | PropertyEffects::Render,
            ValidFlowDirection);

    double FrameworkElement::Width() const { return GetValue(WidthProperty); }
    void FrameworkElement::Width(double value) { SetValue(WidthProperty, value); }
    double FrameworkElement::Height() const { return GetValue(HeightProperty); }
    void FrameworkElement::Height(double value) { SetValue(HeightProperty, value); }
    double FrameworkElement::MinWidth() const { return GetValue(MinWidthProperty); }
    void FrameworkElement::MinWidth(double value) { SetValue(MinWidthProperty, value); }
    double FrameworkElement::MaxWidth() const { return GetValue(MaxWidthProperty); }
    void FrameworkElement::MaxWidth(double value) { SetValue(MaxWidthProperty, value); }
    double FrameworkElement::MinHeight() const { return GetValue(MinHeightProperty); }
    void FrameworkElement::MinHeight(double value) { SetValue(MinHeightProperty, value); }
    double FrameworkElement::MaxHeight() const { return GetValue(MaxHeightProperty); }
    void FrameworkElement::MaxHeight(double value) { SetValue(MaxHeightProperty, value); }
    Thickness FrameworkElement::Margin() const { return GetValue(MarginProperty); }
    void FrameworkElement::Margin(Thickness value) { SetValue(MarginProperty, value); }
    HorizontalAlignment FrameworkElement::HorizontalAlignmentState() const { return GetValue(HorizontalAlignmentProperty); }
    void FrameworkElement::HorizontalAlignmentState(HorizontalAlignment value) { SetValue(HorizontalAlignmentProperty, value); }
    VerticalAlignment FrameworkElement::VerticalAlignmentState() const { return GetValue(VerticalAlignmentProperty); }
    void FrameworkElement::VerticalAlignmentState(VerticalAlignment value) { SetValue(VerticalAlignmentProperty, value); }

    FlowDirection FrameworkElement::FlowDirectionState() const
    {
        if (HasValueOverride(FlowDirectionProperty.Erase())) { return GetValue(FlowDirectionProperty); }
        if (auto parent = Parent())
        {
            return parent->IsRightToLeft() ? FlowDirection::RightToLeft : FlowDirection::LeftToRight;
        }
        return GetValue(FlowDirectionProperty);
    }

    void FrameworkElement::FlowDirectionState(FlowDirection value) { SetValue(FlowDirectionProperty, value); }

    bool FrameworkElement::IsRightToLeft() const
    {
        return FlowDirectionState() == FlowDirection::RightToLeft;
    }

    void FrameworkElement::OnPropertyChanged(PropertyChange const& change)
    {
        UIElement::OnPropertyChanged(change);
        if (change.property == FlowDirectionProperty.Id())
        {
            InvalidateInheritedFlowDirection();
        }
        if (change.property == RequestedThemeProperty.Id()) { RefreshResources(); }
    }

    std::int64_t FrameworkElement::AddSizeChangedHandler(SizeChangedHandler handler)
    {
        VerifyAccess();
        auto lifetime = get_strong();
        if (!handler) { throw winrt::hresult_invalid_argument(); }
        if (m_nextSizeToken == std::numeric_limits<std::int64_t>::max())
        {
            throw winrt::hresult_illegal_method_call();
        }
        auto const token = m_nextSizeToken + 1;
        m_sizeHandlers.push_back(std::make_shared<SizeSubscription>(token, std::move(handler)));
        m_nextSizeToken = token;
        return token;
    }

    void FrameworkElement::RemoveSizeChangedHandler(std::int64_t token)
    {
        VerifyAccess();
        auto lifetime = get_strong();
        auto const found = std::ranges::find_if(m_sizeHandlers,
            [token](auto const& subscription) { return subscription->token == token; });
        if (found == m_sizeHandlers.end()) { return; }
        auto removed = std::move(*found);
        removed->active = false;
        m_sizeHandlers.erase(found);
    }

    void FrameworkElement::OnActualSizeChanged(Size previous, Size current)
    {
        auto lifetime = get_strong();
        auto const handlers = m_sizeHandlers;
        for (auto const& subscription : handlers)
        {
            if (subscription->active) { subscription->handler(*this, previous, current); }
        }
    }

    Size FrameworkElement::MeasureCore(Size available)
    {
        auto const width = Width();
        auto const height = Height();
        auto const margin = Margin();
        auto const horizontal = margin.left + margin.right;
        auto const vertical = margin.top + margin.bottom;
        auto const innerWidth = ConstrainedSpace(available.width, horizontal);
        auto const innerHeight = ConstrainedSpace(available.height, vertical);
        Size constraint{
            std::min(innerWidth, EffectiveLength(std::isnan(width) ? innerWidth : width, MinWidth(), MaxWidth())),
            std::min(innerHeight, EffectiveLength(std::isnan(height) ? innerHeight : height, MinHeight(), MaxHeight())) };
        auto const content = MeasureOverride(constraint);
        ValidateSize(content, false);
        return {
            std::min(available.width, std::max(0.0, EffectiveLength(std::isnan(width) ? content.width : width,
                MinWidth(), MaxWidth()) + horizontal)),
            std::min(available.height, std::max(0.0, EffectiveLength(std::isnan(height) ? content.height : height,
                MinHeight(), MaxHeight()) + vertical)) };
    }

    Size FrameworkElement::ArrangeCore(Size finalSize)
    {
        auto const width = Width();
        auto const height = Height();
        auto const margin = Margin();
        auto const horizontal = HorizontalAlignmentState();
        auto const vertical = VerticalAlignmentState();
        auto const innerWidth = ConstrainedSpace(finalSize.width, margin.left + margin.right);
        auto const innerHeight = ConstrainedSpace(finalSize.height, margin.top + margin.bottom);
        auto const desired = DesiredSize();
        auto const contentWidth = std::max(0.0, desired.width - margin.left - margin.right);
        auto const contentHeight = std::max(0.0, desired.height - margin.top - margin.bottom);
        auto const arrangedWidth = EffectiveLength(std::isnan(width) ?
            (horizontal == HorizontalAlignment::Stretch ? innerWidth : contentWidth) : width, MinWidth(), MaxWidth());
        auto const arrangedHeight = EffectiveLength(std::isnan(height) ?
            (vertical == VerticalAlignment::Stretch ? innerHeight : contentHeight) : height, MinHeight(), MaxHeight());
        auto const result = ArrangeOverride({ arrangedWidth, arrangedHeight });
        ValidateSize(result, false);
        auto const horizontalSpace = innerWidth - result.width;
        auto const verticalSpace = innerHeight - result.height;
        auto const x = margin.left + (horizontal == HorizontalAlignment::Right ? horizontalSpace :
            horizontal == HorizontalAlignment::Center ? horizontalSpace / 2 : 0);
        auto const y = margin.top + (vertical == VerticalAlignment::Bottom ? verticalSpace :
            vertical == VerticalAlignment::Center ? verticalSpace / 2 : 0);
        SetLayoutOffset(x, y);
        return result;
    }

    Size FrameworkElement::MeasureOverride(Size) { return {}; }
    Size FrameworkElement::ArrangeOverride(Size finalSize) { return finalSize; }
}
