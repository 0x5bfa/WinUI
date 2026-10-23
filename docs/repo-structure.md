# Repository Structure

This repository rebuilds the Windows XAML runtime around one native object model. Source folders reflect implementation responsibilities; they do not determine public WinRT namespaces.

## Layout

```text
src/
├─ runtime/
│  ├─ Runtime.ixx
│  ├─ primitives/
│  │  ├─ Object/
│  │  ├─ DependencyProperty/
│  │  ├─ DependencyObject/
│  │  ├─ UIElement/
│  │  └─ FrameworkElement/
│  └─ rendering/              # Planned; no implementation yet
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
- `runtime/rendering/` is a proposed destination for the renderer. **Before any rendering implementation**, re-investigate upstream's render walk, Composition tree, graphics devices/surfaces, frame scheduling, design notes, and history. Compare alternatives and document the architecture and responsibilities first. Avoid mechanically reproducing the old HW/DComp node model.
- `controls/` owns concrete controls, from `Control`, `ButtonBase`, and `Panel` to future `NavigationView` and `ItemsRepeater`. Each control with XAML resources will store them alongside its native implementation. Shared theme/template infrastructure can be introduced when needed.
- `Xaml.Runtime` remains a single named C++ module with internal partitions. `Xaml.Controls` imports Runtime; Runtime never imports Controls. Both currently build as separate static libraries. Source folders do not imply new C++ modules, projects, DLLs, or Core/DXaml peers.

The current native slice includes local value properties, visual-tree ownership, basic measure/arrange, and the control hierarchy through Button. Public XAML/WinRT contracts, templates, managed reference tracking, platform rendering/input, and complete runtime lifecycle are not implemented. See [Native control kernel](architecture/native-control-kernel.md).

## Build

Use Visual Studio 2026, MSVC, `/std:c++latest`, C++ Modules, `Microsoft.UI.Xaml.slnx`, and MSBuild. Keep generated and intermediate files in build output. Documentation and comments are written in English.
