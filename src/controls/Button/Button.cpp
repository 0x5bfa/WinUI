module Xaml.Controls;

import std;
import winrt_base;
import Xaml.Runtime;

namespace xaml::controls
{
    namespace
    {
        std::shared_ptr<ControlTemplate> ButtonTemplate()
        {
            return std::make_shared<ControlTemplate>([](Control& owner)
            {
                TemplateInstance instance;
                auto border = winrt::make_self<Border>();
                auto presenter = winrt::make_self<ContentPresenter>();
                instance.names->Register(L"Border", *border);
                instance.names->Register(L"ContentPresenter", *presenter);
                border->Child(presenter.get());
                instance.root = border;
                auto bind = [&](auto const& source, runtime::DependencyObject& target, auto const& property)
                {
                    instance.bindings.push_back(runtime::PropertyBinding::Connect(owner, source.Erase(), target,
                        property.Erase(), runtime::BindingMode::OneWay, {}, {}, true));
                };
                bind(Control::BackgroundProperty, *border, Border::BackgroundProperty);
                bind(Control::BorderBrushProperty, *border, Border::BorderBrushProperty);
                bind(Control::BorderThicknessProperty, *border, Border::BorderThicknessProperty);
                bind(Control::CornerRadiusProperty, *border, Border::CornerRadiusProperty);
                bind(Control::PaddingProperty, *border, Border::PaddingProperty);
                instance.states = {
                    { L"CommonStates", {
                        { L"Normal", {} },
                        { L"PointerOver", {{ L"Border", { Border::BackgroundProperty, runtime::Color::FromARGB(0xFFE5E5E5) } }} },
                        { L"Pressed", {{ L"Border", { Border::BackgroundProperty, runtime::Color::FromARGB(0xFFD5D5D5) } }} },
                        { L"Disabled", {{ L"Border", { Border::OpacityProperty, 0.45 } }} } } },
                    { L"FocusStates", {
                        { L"Unfocused", {} },
                        { L"Focused", {{ L"Border", { Border::BorderBrushProperty, runtime::Color::FromARGB(0xFF0078D4) } }} } } }
                };
                return instance;
            });
        }
    }
    Button::Button()
    {
        auto defaults = runtime::ResourceDictionary::Defaults();
        if (!defaults->Lookup(L"Button"))
        {
            auto style = runtime::Style::For<Button>();
            style->Add({ TemplateProperty, ButtonTemplate() });
            style->Add({ PaddingProperty, runtime::Thickness{ 12, 6, 12, 6 } });
            style->Add({ MinHeightProperty, 32.0 });
            defaults->Insert(L"Button", std::move(style));
        }
        DefaultStyleKey(L"Button");
    }

    ButtonVisualState Button::VisualState() const
    {
        if (!IsEnabled()) { return ButtonVisualState::Disabled; }
        if (IsPressed()) { return ButtonVisualState::Pressed; }
        return IsPointerOver() ? ButtonVisualState::PointerOver : ButtonVisualState::Normal;
    }
}
