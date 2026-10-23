module Xaml.Controls;

import std;
import winrt_base;
import Xaml.Runtime;

namespace xaml::controls
{
    namespace
    {
        bool ValidSpacing(double value) noexcept
        {
            return std::isfinite(value) && value >= 0;
        }

        bool ValidOrientation(controls::Orientation value) noexcept
        {
            return value == controls::Orientation::Vertical || value == controls::Orientation::Horizontal;
        }
    }

    runtime::DependencyProperty<controls::Orientation> const StackPanel::OrientationProperty =
        runtime::DependencyProperty<controls::Orientation>::Register<StackPanel>("Orientation",
            controls::Orientation::Vertical, runtime::PropertyEffects::Measure, ValidOrientation);

    runtime::DependencyProperty<double> const StackPanel::SpacingProperty =
        runtime::DependencyProperty<double>::Register<StackPanel>("Spacing", 0,
            runtime::PropertyEffects::Measure, ValidSpacing);

    controls::Orientation StackPanel::Orientation() const { return GetValue(OrientationProperty); }
    void StackPanel::Orientation(controls::Orientation value) { SetValue(OrientationProperty, value); }
    double StackPanel::Spacing() const { return GetValue(SpacingProperty); }
    void StackPanel::Spacing(double value) { SetValue(SpacingProperty, value); }

    runtime::Size StackPanel::MeasureOverride(runtime::Size available)
    {
        bool const vertical = Orientation() == controls::Orientation::Vertical;
        double const spacing = Spacing();
        double major{};
        double minor{};
        std::size_t measured{};
        auto const children = SnapshotChildren();
        for (auto const& child : children)
        {
            if (child->Parent().get() != this) { continue; }
            child->Measure(vertical ? runtime::Size{ available.width, std::numeric_limits<double>::infinity() } :
                runtime::Size{ std::numeric_limits<double>::infinity(), available.height });
            if (child->Parent().get() != this) { continue; }
            auto const desired = child->DesiredSize();
            major += (vertical ? desired.height : desired.width) + (measured ? spacing : 0);
            minor = std::max(minor, vertical ? desired.width : desired.height);
            ++measured;
        }
        if (!std::isfinite(major)) { throw winrt::hresult_invalid_argument(); }
        return vertical ? runtime::Size{ minor, major } : runtime::Size{ major, minor };
    }

    runtime::Size StackPanel::ArrangeOverride(runtime::Size finalSize)
    {
        bool const vertical = Orientation() == controls::Orientation::Vertical;
        double const spacing = Spacing();
        double offset{};
        auto const children = SnapshotChildren();
        for (auto const& child : children)
        {
            if (child->Parent().get() != this) { continue; }
            auto const desired = child->DesiredSize();
            auto const major = vertical ? desired.height : desired.width;
            auto const slot = vertical ? runtime::Rect{ 0, offset, finalSize.width, major } :
                runtime::Rect{ offset, 0, major, finalSize.height };
            if (!std::isfinite(offset) || !std::isfinite(major)) { throw winrt::hresult_invalid_argument(); }
            child->Arrange(slot);
            offset += major + spacing;
        }
        return finalSize;
    }
}
