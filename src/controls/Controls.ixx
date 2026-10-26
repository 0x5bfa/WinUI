export module Xaml.Controls;

import std;
import winrt_base;
export import Xaml.Runtime;

export namespace xaml::controls
{
    struct Control;
    struct TemplateSetter
    {
        std::wstring targetName;
        runtime::Setter setter;
    };
    struct VisualState
    {
        std::wstring name;
        std::vector<TemplateSetter> setters;
    };
    struct VisualStateGroup
    {
        std::wstring name;
        std::vector<VisualState> states;
    };
    struct TemplateInstance
    {
        winrt::com_ptr<runtime::UIElement> root;
        std::shared_ptr<runtime::NameScope> names = std::make_shared<runtime::NameScope>();
        std::vector<std::shared_ptr<runtime::PropertyBinding>> bindings;
        std::vector<VisualStateGroup> states;
    };
    class ControlTemplate final
    {
    public:
        using Factory = std::function<TemplateInstance(Control&)>;
        explicit ControlTemplate(Factory factory);
        [[nodiscard]] TemplateInstance Instantiate(Control& owner) const;
    private:
        Factory m_factory;
    };

    struct Control : winrt::implements<Control, runtime::FrameworkElement>
    {
        ~Control();
        static runtime::DependencyProperty<std::shared_ptr<ControlTemplate>> const TemplateProperty;
        static runtime::DependencyProperty<runtime::Color> const BackgroundProperty;
        static runtime::DependencyProperty<runtime::Color> const ForegroundProperty;
        static runtime::DependencyProperty<runtime::Color> const BorderBrushProperty;
        static runtime::DependencyProperty<double> const BorderThicknessProperty;
        static runtime::DependencyProperty<double> const CornerRadiusProperty;
        static runtime::DependencyProperty<runtime::Thickness> const PaddingProperty;
        static runtime::DependencyProperty<bool> const IsTabStopProperty;
        static runtime::DependencyProperty<int> const TabIndexProperty;

        [[nodiscard]] runtime::Thickness Padding() const;
        void Padding(runtime::Thickness value);
        [[nodiscard]] bool IsFocused() const;
        void SetFocused(bool focused);
        [[nodiscard]] bool IsTabStop() const;
        void IsTabStop(bool value);
        [[nodiscard]] int TabIndex() const;
        void TabIndex(int value);
        [[nodiscard]] bool IsKeyboardFocusable() const override;
        [[nodiscard]] int KeyboardTabIndex() const override;
        void Template(std::shared_ptr<ControlTemplate> value);
        [[nodiscard]] std::shared_ptr<ControlTemplate> Template() const;
        bool ApplyTemplate();
        [[nodiscard]] winrt::com_ptr<runtime::DependencyObject> GetTemplateChild(std::wstring_view name) const;
        bool GoToState(std::wstring_view name);
        void DefaultStyleKey(std::wstring key);
        [[nodiscard]] std::wstring_view DefaultStyleKey() const;

    protected:
        void OnIsEnabledChanged() override;
        void OnVisualParentChanged() override;
        void OnVisualAncestryChanged() override;
        virtual void OnFocusLost() {}
        void OnInputEvent(runtime::InputEventArgs& args) override;
        void OnLoading() override;
        void OnPropertyChanged(runtime::PropertyChange const& change) override;
        void OnResourcesChanged() override;
        void OnStyleChanged() override;
        runtime::Size MeasureOverride(runtime::Size available) override;
        runtime::Size ArrangeOverride(runtime::Size finalSize) override;
        virtual void OnApplyTemplate() {}
        virtual void OnTemplateClearing() {}
        virtual void UpdateVisualState();
        void ClearTemplate();
        [[nodiscard]] bool HasTemplateRoot() const;

    private:
        bool m_focused{};
        std::optional<TemplateInstance> m_instance;
        std::uint64_t m_templateVersion{};
        bool m_applyingTemplate{};
        bool m_clearingTemplate{};
        std::wstring m_defaultStyleKey;
        std::shared_ptr<runtime::Style> m_defaultStyle;
        bool m_applyingDefaultStyle{};
        struct StateApplication
        {
            std::wstring state;
            std::uint64_t token{};
            std::vector<std::pair<winrt::weak_ref<runtime::DependencyObject>, runtime::DependencyPropertyHandle>> targets;
        };
        std::map<std::wstring, StateApplication> m_states;
        void RefreshDefaultStyle();
    };

    struct ContentControl : winrt::implements<ContentControl, Control>
    {
        [[nodiscard]] winrt::com_ptr<runtime::UIElement> Content() const;
        void Content(runtime::UIElement* content);

    protected:
        runtime::Size MeasureOverride(runtime::Size available) override;
        runtime::Size ArrangeOverride(runtime::Size finalSize) override;
        void OnApplyTemplate() override;
        void OnTemplateClearing() override;
    private:
        winrt::com_ptr<runtime::UIElement> m_content;
    };

    struct ContentPresenter : winrt::implements<ContentPresenter, runtime::FrameworkElement>
    {
        void Content(runtime::UIElement* value);
        [[nodiscard]] winrt::com_ptr<runtime::UIElement> Content() const;
        static runtime::DependencyProperty<runtime::Thickness> const PaddingProperty;
    protected:
        runtime::Size MeasureOverride(runtime::Size available) override;
        runtime::Size ArrangeOverride(runtime::Size finalSize) override;
    };

    struct Border : winrt::implements<Border, runtime::FrameworkElement>
    {
        static runtime::DependencyProperty<runtime::Color> const BackgroundProperty;
        static runtime::DependencyProperty<runtime::Color> const BorderBrushProperty;
        static runtime::DependencyProperty<double> const BorderThicknessProperty;
        static runtime::DependencyProperty<double> const CornerRadiusProperty;
        static runtime::DependencyProperty<runtime::Thickness> const PaddingProperty;
        void Child(runtime::UIElement* value);
        [[nodiscard]] winrt::com_ptr<runtime::UIElement> Child() const;
    protected:
        runtime::Size MeasureOverride(runtime::Size available) override;
        runtime::Size ArrangeOverride(runtime::Size finalSize) override;
        void OnRender(runtime::DrawingContext& context) const override;
    };

    struct Image : winrt::implements<Image, runtime::FrameworkElement>
    {
        static runtime::DependencyProperty<winrt::hstring> const SourceProperty;
        static runtime::DependencyProperty<runtime::ImageStretch> const StretchProperty;
        [[nodiscard]] winrt::hstring Source() const;
        void Source(winrt::hstring value);
        [[nodiscard]] runtime::ImageStretch Stretch() const;
        void Stretch(runtime::ImageStretch value);
        [[nodiscard]] runtime::AutomationRole AutomationControlRole() const override { return runtime::AutomationRole::Image; }
    protected:
        runtime::Size MeasureOverride(runtime::Size available) override;
        void OnRender(runtime::DrawingContext& context) const override;
    };

    struct TextBlock : winrt::implements<TextBlock, runtime::FrameworkElement>
    {
        static runtime::DependencyProperty<winrt::hstring> const TextProperty;
        static runtime::DependencyProperty<winrt::hstring> const FontFamilyProperty;
        static runtime::DependencyProperty<winrt::hstring> const LanguageProperty;
        static runtime::DependencyProperty<double> const FontSizeProperty;
        static runtime::DependencyProperty<int> const FontWeightProperty;
        static runtime::DependencyProperty<bool> const TextWrappingProperty;
        static runtime::DependencyProperty<runtime::Color> const ForegroundProperty;
        void Text(winrt::hstring const& value);
        [[nodiscard]] winrt::hstring Text() const;
        [[nodiscard]] runtime::AutomationRole AutomationControlRole() const override { return runtime::AutomationRole::Text; }
        [[nodiscard]] winrt::hstring AutomationLabel() const override;
    protected:
        runtime::Size MeasureOverride(runtime::Size available) override;
        void OnRender(runtime::DrawingContext& context) const override;
    private:
        [[nodiscard]] runtime::TextFormat Format() const;
    };

    struct Panel : winrt::implements<Panel, runtime::FrameworkElement>
    {
        [[nodiscard]] std::size_t ChildrenCount() const;
        [[nodiscard]] winrt::com_ptr<runtime::UIElement> ChildAt(std::size_t index) const;
        void Append(runtime::UIElement* child);
        void Insert(std::size_t index, runtime::UIElement* child);
        void RemoveAt(std::size_t index);
        void Move(std::size_t from, std::size_t to);
        void Clear();

    protected:
        [[nodiscard]] std::vector<winrt::com_ptr<runtime::UIElement>> SnapshotChildren() const;
    };

    enum class Orientation { Vertical, Horizontal };

    struct StackPanel : winrt::implements<StackPanel, Panel>
    {
        static runtime::DependencyProperty<controls::Orientation> const OrientationProperty;
        static runtime::DependencyProperty<double> const SpacingProperty;

        [[nodiscard]] controls::Orientation Orientation() const;
        void Orientation(controls::Orientation value);
        [[nodiscard]] double Spacing() const;
        void Spacing(double value);

    protected:
        runtime::Size MeasureOverride(runtime::Size available) override;
        runtime::Size ArrangeOverride(runtime::Size finalSize) override;
    };

    enum class ClickMode { Release, Press, Hover };
    enum class ActivationKey { Space, Enter };
    enum class PointerAction { Entered, Exited, Pressed, Moved, Released, Cancelled, CaptureLost };

    // Coordinates/hit testing and physical capture belong to the input backend.
    // These are already-targeted, element-relative input facts.
    struct PointerInput
    {
        PointerAction action;
        std::uint32_t id;
        bool isOver;
        bool isPrimaryButton{ true };
    };

    struct ButtonBase : winrt::implements<ButtonBase, ContentControl>
    {
        ~ButtonBase();
        using ClickHandler = std::function<void(ButtonBase&)>;
        using EventToken = std::uint64_t;

        static runtime::DependencyProperty<controls::ClickMode> const ClickModeProperty;
        static runtime::DependencyProperty<std::shared_ptr<runtime::Command>> const CommandProperty;
        static runtime::DependencyProperty<winrt::Windows::Foundation::IInspectable> const CommandParameterProperty;
        [[nodiscard]] controls::ClickMode ClickMode() const;
        void ClickMode(controls::ClickMode value);
        [[nodiscard]] bool IsPressed() const;
        [[nodiscard]] bool IsPointerOver() const;
        [[nodiscard]] std::optional<std::uint32_t> ActivePointer() const;
        [[nodiscard]] EventToken AddClick(ClickHandler handler);
        void RemoveClick(EventToken token);
        bool ProcessPointer(PointerInput const& input);
        bool ProcessKey(ActivationKey key, bool isDown);
        void CancelInteraction();
        [[nodiscard]] runtime::AutomationRole AutomationControlRole() const override { return runtime::AutomationRole::Button; }
        [[nodiscard]] winrt::hstring AutomationLabel() const override;
        bool AutomationInvoke() override;

    protected:
        void OnIsEnabledChanged() override;
        bool IsEnabledCore() const override;
        void OnVisualParentChanged() override;
        void OnVisualAncestryChanged() override;
        void OnFocusLost() override;
        void OnInputEvent(runtime::InputEventArgs& args) override;
        void UpdateVisualState() override;
        void OnPropertyChanged(runtime::PropertyChange const& change) override;
        virtual void OnClick();

    private:
        struct Subscription
        {
            EventToken token;
            ClickHandler handler;
            bool active{ true };
        };
        std::vector<std::shared_ptr<Subscription>> m_click;
        std::optional<std::uint32_t> m_pointer;
        std::optional<ActivationKey> m_key;
        EventToken m_nextToken{};
        bool m_pointerOver{};
        bool m_commandCanExecute{ true };
        std::weak_ptr<runtime::Command> m_observedCommand;
        std::uint64_t m_commandToken{}, m_commandVersion{};
        void RefreshCommand();
    };

    enum class ButtonVisualState { Normal, PointerOver, Pressed, Disabled };

    struct Button : winrt::implements<Button, ButtonBase>
    {
        Button();
        [[nodiscard]] ButtonVisualState VisualState() const;
    };
}
