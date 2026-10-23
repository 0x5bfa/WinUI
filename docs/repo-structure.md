# Repository Structure

This repository contains a new Windows XAML runtime. Public WinRT and XAML compatibility is defined at the boundary; the runtime uses one internal object model.

The layout below is a broad guide, not a fixed directory or project structure. Create only the parts needed by each implementation step, and adjust boundaries as the code develops.

## Layout

```text
/
├─ Microsoft.UI.Xaml.slnx
├─ Directory.Build.props
├─ Directory.Build.targets
├─ eng/
│  ├─ build/
│  └─ toolchain/
├─ docs/
│  ├─ repo-structure.md
│  └─ architecture/
│     ├─ README.md
│     └─ decisions/
├─ src/
│  ├─ contracts/                 # Public API and type/property definitions
│  ├─ tools/
│  │  ├─ XamlGen/                # Generate ABI and runtime metadata
│  │  └─ XamlCompiler/           # Compile XAML markup
│  ├─ platform/
│  │  ├─ contracts/              # Runtime-facing platform interfaces
│  │  └─ windows/                # Windowing, Composition, input, text, UIA
│  ├─ runtime/
│  │  ├─ foundation/
│  │  ├─ object-model/
│  │  ├─ primitives/               # UIElement, FrameworkElement, layout and visual tree
│  │  ├─ scheduling/
│  │  ├─ layout/
│  │  ├─ resources/
│  │  ├─ input/
│  │  ├─ text/
│  │  ├─ automation/
│  │  └─ rendering/
│  ├─ controls/
│  │  ├─ Controls.ixx              # Single native control module interface
│  │  ├─ Control/
│  │  ├─ ContentControl/
│  │  ├─ ButtonBase/
│  │  ├─ Button/                   # Button.cpp; its XAML resources will live here
│  │  ├─ Panel/
│  │  └─ StackPanel/
│  ├─ themes/                      # Shared theme infrastructure (future)
│  ├─ markup/
│  │  └─ runtime/                # Load and execute compiled markup
│  └─ product/
│     └─ Microsoft.UI.Xaml.vcxproj
├─ tests/
│  ├─ unit/
│  ├─ compatibility/
│  ├─ integration/
│  └─ performance/
└─ samples/
   ├─ SmokeApp/
   └─ Gallery/
```

## Boundaries

- `contracts/` is the source of truth for public APIs and type metadata. `XamlGen` produces generated files in build output; do not check generated files into the source tree.
- `runtime/` owns object identity, dependency properties, the native visual tree, layout primitives, and runtime services. `UIElement` and `FrameworkElement` live under `runtime/primitives` in the `Xaml.Runtime:Primitives` partition. Source folders do not prescribe the public WinRT namespace.
- `platform/contracts/` defines services used by the runtime. `platform/windows/` implements them with Windows APIs.
- `controls/` owns all UI controls, from `ButtonBase` and `Panel` to future `NavigationView` and `ItemsRepeater`. A control with XAML resources owns a named directory for both its native behavior and its XAML sources; shared theme composition can live under `themes/`. Resource loading and template realization will be provided by `markup/` and the final product, not by a second control identity.
- `Xaml.Controls` depends on `Xaml.Runtime`, which never imports `Xaml.Controls`. All controls compile into one control module and a separate static library. Folder-per-control does not require module-per-control or DLL-per-control.
- The native implementation currently has two static-library projects: `src/runtime/Microsoft.UI.Xaml.Runtime.vcxproj` and `src/controls/Microsoft.UI.Xaml.Controls.vcxproj`. Their output is linked into native tests, and eventually into the product DLL. Only Runtime builds C++/WinRT projection modules; Controls consumes its compatible IFCs.

The current native slice includes object identity, local value properties (including small value structs), an ordered visual tree, basic measure/arrange, `Panel`/`StackPanel` layout, Control padding, and the control hierarchy through Button. See [the native control kernel](architecture/native-control-kernel.md) for its boundaries. Public XAML/WinRT contracts, template loading, managed reference tracking, platform rendering/input, session lifetime, and UI-affine reclamation remain later slices.

## Build and Documentation

- Use Visual Studio 2026, MSVC, `/std:c++latest`, and MSBuild. Keep `Microsoft.UI.Xaml.slnx` as the root solution and pin the tested toolset in `eng/toolchain/`.
- Keep generated and intermediate files in build output directories.
- Keep documentation concise. Add ADRs under `docs/architecture/decisions/` only for major decisions with long-term architectural impact.
