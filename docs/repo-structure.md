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
│  ├─ input/                  # Element routing, logical capture and focus
│  └─ rendering/              # WUC realization and lifted platform input
└─ controls/
   ├─ Controls.ixx
   ├─ Control/
   ├─ ContentControl/
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

The current native slice includes local and attached value properties, per-object property callbacks, size constraints and margins, visual-tree ownership, basic measure/arrange, and the control hierarchy through Button. WUC realization, lifted input adaptation, native event routing, and a desktop consumer are implemented; their runtime behavior still needs validation. Public XAML/WinRT contracts, templates, managed reference tracking, complete rendering content, and complete runtime lifecycle remain pending. The existing control prototypes are not yet wired to the new input manager. See [Native control kernel](architecture/native-control-kernel.md) and [Primitive WinUI API compatibility](architecture/primitives-api-compatibility.md).

## Build

Use Visual Studio 2026, MSVC, `/std:c++latest`, C++ Modules, `Microsoft.UI.Xaml.slnx`, and MSBuild. Keep generated and intermediate files in build output. Documentation and comments are written in English.
