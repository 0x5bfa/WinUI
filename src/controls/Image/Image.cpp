module Xaml.Controls;

import std;
import winrt_base;
import Xaml.Runtime;

namespace xaml::controls
{
    runtime::DependencyProperty<winrt::hstring> const Image::SourceProperty =
        runtime::DependencyProperty<winrt::hstring>::Register<Image>("Source", L"",
            runtime::PropertyEffects::Measure | runtime::PropertyEffects::Render);

    runtime::DependencyProperty<runtime::ImageStretch> const Image::StretchProperty =
        runtime::DependencyProperty<runtime::ImageStretch>::Register<Image>("Stretch", runtime::ImageStretch::Uniform,
            runtime::PropertyEffects::Measure | runtime::PropertyEffects::Render,
            +[](runtime::ImageStretch value)
            { return value >= runtime::ImageStretch::None && value <= runtime::ImageStretch::UniformToFill; });

    winrt::hstring Image::Source() const { return GetValue(SourceProperty); }
    void Image::Source(winrt::hstring value) { SetValue(SourceProperty, std::move(value)); }
    runtime::ImageStretch Image::Stretch() const { return GetValue(StretchProperty); }
    void Image::Stretch(runtime::ImageStretch value) { SetValue(StretchProperty, value); }

    runtime::Size Image::MeasureOverride(runtime::Size available)
    {
        auto const source = Source();
        if (source.empty()) { return {}; }
        auto const natural = runtime::MeasureImage(std::wstring(source.c_str()));
        if (natural.width <= 0 || natural.height <= 0) { return {}; }
        auto const availableWidth = std::min(available.width, natural.width);
        auto const availableHeight = std::min(available.height, natural.height);
        if (Stretch() == runtime::ImageStretch::Fill || Stretch() == runtime::ImageStretch::None)
        {
            return { availableWidth, availableHeight };
        }
        auto const factor = Stretch() == runtime::ImageStretch::Uniform ?
            std::min(availableWidth / natural.width, availableHeight / natural.height) :
            std::max(availableWidth / natural.width, availableHeight / natural.height);
        return { natural.width * factor, natural.height * factor };
    }

    void Image::OnRender(runtime::DrawingContext& drawing) const
    {
        auto const source = Source();
        auto const size = ActualSize();
        if (!source.empty() && size.width > 0 && size.height > 0)
        {
            drawing.Image(0, 0, size.width, size.height, std::wstring(source.c_str()), Stretch());
        }
    }
}
