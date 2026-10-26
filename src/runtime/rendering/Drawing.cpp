module;
#include "DrawingTypes.h"

module Xaml.Runtime;

import std;
import winrt_base;

namespace xaml::runtime
{
    TextMetrics MeasureText(std::wstring const& text, TextFormat const& format, double width, double height)
    {
        return XamlMeasureText(text, format, width, height);
    }
    ImageMetrics MeasureImage(std::wstring const& filePath)
    {
        return XamlMeasureImage(filePath);
    }
    DrawingContext UIElement::CollectDrawingContent() const
    {
        VerifyAccess();
        DrawingContext context;
        OnRender(context);
        return context;
    }
}
