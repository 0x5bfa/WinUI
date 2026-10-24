# Control authoring foundations

## Scope and reference

The native authoring layer uses one object per element, WUC composition, and lifted input. This is not yet the published WinRT ABI. Public contracts, managed composition/reference tracking, packaging, and IME remain separate delivery gates; building a native control does not establish those gates.

Reference: `D:/source/mux-main`, revision `0df0a8869`. Read these paths before changing the corresponding behavior:

| Responsibility | Upstream implementation | Unified implementation |
| --- | --- | --- |
| Template instantiation and parts | `dxaml/xcp/core/core/elements/Control.cpp`, `dxaml/xcp/dxaml/lib/Control_Partial.cpp` | A factory returns one visual root, a namescope, bindings, and state groups. The owning Control releases the entire instance on replacement. |
| Button interaction | `dxaml/xcp/dxaml/lib/ButtonBase_Partial.cpp`, `dxaml/xcp/components/controls/KeyDownUp/inc/ButtonBaseKeyProcess.h` | ButtonBase consumes routed input and shares root focus/capture. Press state is committed before invoking user handlers. |
| Commands | `dxaml/xcp/dxaml/lib/ButtonBase_Partial.cpp::UpdateCanExecute`, `OnClick` | CanExecute coerces the effective enabled state without overwriting the local IsEnabled value. Click handlers run before command execution. The native Command is not the public ICommand projection. |
| Tab scopes | `dxaml/xcp/core/dll/focusmgr.cpp::GetNextTabStop` | Root traversal supports TabIndex, Cycle, Once, and weak remembered focus. Cross-island host departure and directional navigation remain separate. |
| Template bindings | `dxaml/xcp/core/core/elements/framework.cpp` | Weak endpoints, expression tokens, and explicit disconnect. Local values override template values. |
| Visual state setters | `dxaml/xcp/components/vsm/VisualStateManagerActuator.cpp` | Each state application owns its property contributions. Replacement and reentrant transitions retire only their own contributions. Timed transitions are not implemented. |
| Border layout | `dxaml/xcp/core/core/elements/Border.cpp` | One visual child; border and padding reduce the child's measure/arrange bounds. |
| Text measurement | `dxaml/xcp/core/text/TextBlock/TextBlock.cpp::ConfigureDWriteTextLayout` | DirectWrite performs shaping, fallback, wrapping, and measurement. No separate text layout engine. |
| Editable text hosting | `dxaml/xcp/core/native/text/Controls/TextServicesHost.cpp`, `TextBoxBase.cpp` | Upstream loads WinUIEdit.dll/RichEdit. The new plain-text foundation uses desktop TSF with a focused TextInputClient; this does not reproduce RichEdit features. |
| Visual content | `dxaml/xcp/core/hw/VisualContentRenderer.cpp`, `dxaml/xcp/components/comptree/DCompTreeHost.cpp` | Element drawing commands contain values, not a second element object. WUC owns composition resources. |

## Drawing boundary

An element records local-DIP rectangle/text commands after layout. A renderer retains WUC resources keyed by element identity and render version. Rectangles use WUC rounded-rectangle shapes. Text uses DirectWrite layout and a Direct2D drawing surface supplied through WUC's public interop interfaces. There is no custom swap chain, presentation thread, compositor, or shader pipeline.

Drawing surfaces are rasterized at the content root's scale. Their size/version/scale participate in invalidation. Surface BeginDraw/EndDraw is balanced even when drawing fails; returned contexts are never cached. The backend owns graphics resources without retaining framework objects. Device replacement invalidates text surfaces together. Element geometry and root identity survive replacement.

RTL boundary transforms arrange descendants, while text content cancels the cumulative reflection and uses DirectWrite reading direction. A text glyph must never be mirrored by an inherited layout transform.

## Property and resource rules

Effective priority is animation, state setter, local/expression, template, style, inherited, default. `SetValue` replaces a local expression; `SetCurrentValue` changes its current value without removing the expression. A template binding is a one-way property binding in the template layer. Resource references remain installed when a key is temporarily missing. Reparenting re-evaluates resource lookup and inheritable properties.

Styles seal before use and validate every setter against its target. Templates have an isolated namescope and weak templated-parent links. Template clearing prevents recursive application and finishes cleanup after callback failures. Control authors override layout, input, drawing, template application, and visual-state selection on the same object.

## Native authoring example

Use `winrt::make_self<T>()` for native elements. A simple custom drawing element needs only FrameworkElement, a property, measurement, and drawing:

```cpp
import Xaml.Controls;

struct Badge : winrt::implements<Badge, xaml::runtime::FrameworkElement>
{
    static inline auto ValueProperty =
        xaml::runtime::DependencyProperty<int>::Register<Badge>(
            "Value", 0, xaml::runtime::PropertyEffects::Render);

    xaml::runtime::Size MeasureOverride(xaml::runtime::Size) override
    { return { 48, 24 }; }

    void OnRender(xaml::runtime::DrawingContext& drawing) const override
    {
        auto size = ActualSize();
        drawing.Rectangle(0, 0, size.width, size.height,
            xaml::runtime::Color::FromARGB(0xFF0078D4), 4);
        drawing.Text(4, 2, std::max(0.0, size.width - 8), std::max(0.0, size.height - 4),
            std::to_wstring(GetValue(ValueProperty)), {},
            xaml::runtime::Color::FromARGB(0xFFFFFFFF));
    }
};
```

For a templated control, derive from Control or ContentControl. Set `Template` or provide a Style under `DefaultStyleKey` in an ancestor/application dictionary. A ControlTemplate factory returns its root and namescope; register named parts in that scope and keep property bindings in `TemplateInstance::bindings`. Connect owner properties with `PropertyBinding::Connect(..., BindingMode::OneWay, {}, {}, true)` to put them in the template layer. Override `OnApplyTemplate` to resolve parts and `OnTemplateClearing` to detach subscriptions/content. `Button.cpp` provides an actual factory using Border, ContentPresenter, template bindings, and common/focus state setters.

For model-to-property synchronization, the current native API binds DependencyObject properties directly, optionally with converters and two-way updates. A control uses `SetCurrentValue` for edits that should preserve a binding. Property paths, INotifyPropertyChanged/collection change adapters, general-object Content/DataTemplate, and the public XAML Binding classes are not implemented by this native API.

## Implementation status

| Foundation | Current status |
| --- | --- |
| Native identity, property storage, layout and visual tree | Implemented; no Core/DXaml peers. |
| Native resources, inherited DataContext/theme, styles and property bindings | Implemented. Default resolves to Light; automatic OS theme/high-contrast synchronization is pending. |
| Native templates, parts, TemplateBinding and state setters | Implemented. Timed transitions and Storyboard are pending. |
| Native drawing and basic template parts | Rectangle/rounded border and DirectWrite text; no general brush hierarchy, images, effects or arbitrary transforms. |
| Native routed input, focus/capture and commands | Connected to controls. Tab scopes implemented; complete host/directional navigation is pending. |
| IME connection | Plain TSF text store and client contract implemented. No native text editor, rich attributes, or IME runtime validation. |
| Native NuGet consumption | Source package and standalone consumer. No prebuilt IFC redistribution. |
| Public WinRT ABI and C#/C++ composability | Not implemented. Native methods do not establish this compatibility. |
| Accessibility, full XAML binding/content, virtualization and other control services | Pending. |

The user-requested complete control-authoring foundation remains open until the pending capabilities and public compatibility gates are delivered. This implementation is a substantial native foundation slice, not a completion claim for that full request.

### Build evidence (2026-10-25)

- Runtime, Controls, and NativeDesktop compiled and linked in Debug/Release x64.
- `dotnet pack eng/packaging/Unified.WinUI.Native.csproj -c Release` produces the native source package.
- `Unified.WinUI.Native.0.1.0-preview.3.nupkg` built an isolated PackagedDesktop consumer in Debug/Release x64 under `artifacts/native-package-consumer-preview3`. Empty local Directory.Build.props/targets excluded repository build settings. Its source and generated projections came from the package and pinned SDK dependencies, with no ProjectReference or repository IFC reuse.
- Applications were not launched. Tests were not added or run. IME, visual output, theme behavior, accessibility, reference tracking, and public ABI compatibility have no validation evidence in this change.

## Remaining delivery gates

- Real baseline WinRT interfaces, original IIDs, factories/composition, and managed reference tracking.
- A NuGet-only external consumer, including generated projections and deployment assets.
- Desktop IME with a text-store/selection/composition contract; key events alone are insufficient.
- Full focus scopes/host traversal, automation, commands, text editing, images, transforms, animations, and public XAML-facing collections and bindings.
- Behavioral and visual validation. Builds establish compilation only.

## Platform references

- [WUC graphics-device interop](https://learn.microsoft.com/en-us/windows/win32/api/windows.ui.composition.interop/nf-windows-ui-composition-interop-icompositorinterop-creategraphicsdevice)
- [Microsoft's WUC surface interop sample](https://github.com/microsoft/Windows.UI.Composition-Win32-Samples/blob/master/cpp/ScreenCaptureforHWND/ScreenCaptureforHWND/composition.interop.h)
- [TSF text stores](https://learn.microsoft.com/en-us/windows/win32/tsf/text-stores)
- [TSF document lock requests](https://learn.microsoft.com/en-us/windows/win32/api/textstor/nf-textstor-itextstoreacp-requestlock)
