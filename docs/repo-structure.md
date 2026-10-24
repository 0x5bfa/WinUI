# Repository Structure

This repository rebuilds the Windows XAML runtime around one native object model. Source folders reflect implementation responsibilities; they do not determine public WinRT namespaces.

## Layout

```text
src/
├─ runtime/
│  ├─ Runtime.ixx
│  ├─ ContentRoot.ixx
│  ├─ DesktopWindowHost.ixx
│  ├─ primitives/
│  │  ├─ DependencyProperty/
│  │  ├─ DependencyObject/
│  │  ├─ UIElement/
│  │  └─ FrameworkElement/
│  ├─ binding/                # Native property expressions
│  ├─ styling/                # Resources, styles and namescopes
│  ├─ input/                  # Routing, focus, commands and desktop TSF
│  └─ rendering/              # WUC realization and lifted platform input
└─ controls/
   ├─ Controls.ixx
   ├─ Control/
   ├─ ContentControl/
   ├─ Border/
   ├─ TextBlock/
   ├─ ButtonBase/
   ├─ Button/
   ├─ Panel/
   └─ StackPanel/
```

Add runtime directories when their implementations need them. Do not create speculative subdivisions.

## Boundaries

- `runtime/primitives/` owns native object identity, dependency properties, visual-tree participation, and base-element layout. Each base type has a named folder. Attached property registration belongs with `DependencyProperty`; owner-specific attached properties (e.g. `Grid.Row`) belong to the owning type. The current native implementation uses the internal `xaml::runtime` namespace; public types will follow their `Microsoft.UI.Xaml` contracts.
- `runtime/input/` owns element routing, logical pointer capture, and logical focus. `runtime/rendering/` realizes element state into WUC visuals and adapts lifted platform input into native input facts. `ContentRoot` and `DesktopWindowHost` own root updates and HWND hosting. See [Framework foundations](architecture/framework-foundations.md) for the current implementation and limitations. Avoid mechanically reproducing the old HW/DComp node model.
- `controls/` owns concrete controls, from `Control`, `ButtonBase`, and `Panel` to future `NavigationView` and `ItemsRepeater`. Each control with XAML resources will store them alongside its native implementation. Shared theme/template infrastructure can be introduced when needed.
- `Xaml.Runtime` remains a single named C++ module with internal partitions. `Xaml.Controls` imports Runtime; Runtime never imports Controls. Both currently build as separate static libraries. Source folders do not imply new C++ modules, projects, DLLs, or Core/DXaml peers.

The native slice includes dependency properties and inheritance, layout, property bindings, resources/styles, isolated template namescopes, template/state setters, WUC rectangle/text drawing, lifted routed input, Tab scopes, commands, and a TSF text-store connection. Button/ContentControl/Border/TextBlock use these shared foundations. NativeDesktop consumes projects; PackagedDesktop consumes the native source NuGet. These are native implementations whose runtime behavior still needs validation. Public WinRT contracts, managed composition/reference tracking, automation, and complete rendering/text/control behavior remain pending. See [Control authoring](architecture/control-authoring.md) and [Primitive WinUI API compatibility](architecture/primitives-api-compatibility.md).

## Build

Use Visual Studio 2026, MSVC, `/std:c++latest`, C++ Modules, `Microsoft.UI.Xaml.slnx`, and MSBuild. Keep generated and intermediate files in build output. Documentation and comments are written in English.
