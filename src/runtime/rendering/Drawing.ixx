module;
#include "DrawingTypes.h"

export module Xaml.Runtime:Drawing;

export namespace xaml::runtime
{
    using Color = xaml::drawing::Color;
    using TextFormat = xaml::drawing::TextFormat;
    using TextMetrics = xaml::drawing::TextMetrics;
    using ImageMetrics = xaml::drawing::ImageMetrics;
    using ImageStretch = xaml::drawing::ImageStretch;
    using DrawingContext = xaml::drawing::Context;
    using DrawingCommand = xaml::drawing::Command;
    [[nodiscard]] TextMetrics MeasureText(std::wstring const& text, TextFormat const& format,
        double width, double height);
    [[nodiscard]] ImageMetrics MeasureImage(std::wstring const& filePath);
}
