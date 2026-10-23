module Xaml.Controls;

import std;
import winrt_base;
import Xaml.Runtime;

namespace xaml::controls
{
    ButtonVisualState Button::VisualState() const
    {
        if (!IsEnabled()) { return ButtonVisualState::Disabled; }
        if (IsPressed()) { return ButtonVisualState::Pressed; }
        return IsPointerOver() ? ButtonVisualState::PointerOver : ButtonVisualState::Normal;
    }
}
